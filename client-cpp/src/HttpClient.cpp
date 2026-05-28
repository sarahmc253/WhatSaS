#include "tcp_connect.hpp"
#include "tls_connect.hpp"
#include "http_response.hpp"
#include "../include/HttpClient.hpp"

#include <openssl/err.h>
#include <iostream>
#include <memory>

// RAII wrappers for OpenSSL SSL* and raw SOCKET

struct SslDeleter { void operator()(SSL* s) const { if (s) SSL_free(s); } };
using UniqueSSL = std::unique_ptr<SSL, SslDeleter>;

struct RaiiSocket {
    SOCKET fd;
    explicit RaiiSocket(SOCKET s) noexcept : fd(s) {}
    ~RaiiSocket() { if (fd != INVALID_SOCKET) closesocket(fd); }
    RaiiSocket(const RaiiSocket&)            = delete;
    RaiiSocket& operator=(const RaiiSocket&) = delete;
};

// SSL_CTX deleter (declared in HttpClient.hpp, implemented here)

void HttpClient::SslCtxDeleter::operator()(SSL_CTX* ctx) const {
    if (ctx) SSL_CTX_free(ctx);
}

// Constructor / destructor / move

HttpClient::HttpClient() : wsaInitialized_(false), ctx_(nullptr) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return;
    }
    wsaInitialized_ = true;

    SSL_CTX* raw = createSslCtx();
    if (!raw) {
        std::cerr << "SSL_CTX creation failed\n";
        return;
    }
    ctx_.reset(raw);
}

HttpClient::~HttpClient() {
    // ctx_ unique_ptr destructor runs first (SSL_CTX_free), then WSACleanup
    if (wsaInitialized_) WSACleanup();
}

HttpClient::HttpClient(HttpClient&& other) noexcept
    : wsaInitialized_(other.wsaInitialized_), ctx_(std::move(other.ctx_)) {
    other.wsaInitialized_ = false;
}

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept {
    if (this != &other) {
        if (wsaInitialized_) WSACleanup();
        ctx_ = std::move(other.ctx_);
        wsaInitialized_ = other.wsaInitialized_;
        other.wsaInitialized_ = false;
    }
    return *this;
}

// Private: shared TCP → TLS → write → read → parse pipeline

HttpResponse HttpClient::doRequest(const std::string& host,
                                   const std::string& port,
                                   const std::string& requestStr,
                                   bool verifyCert) const {
    std::string err;
    SOCKET fd = connectTcp(host, port, err);
    if (fd == INVALID_SOCKET) {
        return {0, "", err, false};
    }
    RaiiSocket sock(fd);

    SSL* sslRaw = performTlsHandshake(ctx_.get(), sock.fd, host, verifyCert, err);
    if (!sslRaw) {
        return {0, "", err, false};
    }
    UniqueSSL ssl(sslRaw);

    if (SSL_write(ssl.get(), requestStr.data(), static_cast<int>(requestStr.size()))
            != static_cast<int>(requestStr.size())) {
        unsigned long e = ERR_get_error();
        return {0, "", e ? ERR_error_string(e, nullptr) : "SSL_write incomplete", false};
    }

    static constexpr std::size_t MAX_RESPONSE_BYTES = 10 * 1024 * 1024;

    std::string raw;
    char buf[4096];
    int n;
    while ((n = SSL_read(ssl.get(), buf, sizeof(buf))) > 0) {
        raw.append(buf, n);
        if (raw.size() > MAX_RESPONSE_BYTES) {
            return {0, "", "Response exceeded size limit", false};
        }
    }

    // n == 0: clean TLS close_notify — normal end; n < 0: real SSL error
    if (n < 0) {
        int sslErr = SSL_get_error(ssl.get(), n);
        if (sslErr != SSL_ERROR_ZERO_RETURN) {
            unsigned long e = ERR_get_error();
            return {0, "", e ? ERR_error_string(e, nullptr) : "SSL_read error", false};
        }
    }

    return parseResponse(raw);
}

// Public API

HttpResponse HttpClient::get(const std::string& url,
                             const std::string& authToken,
                             bool verifyCert) const {
    if (!wsaInitialized_ || !ctx_) {
        return {0, "", "HttpClient not initialized", false};
    }
    ERR_clear_error();

    ParsedUrl parsed = parseUrl(url);
    if (!parsed.error.empty()) {
        return {0, "", parsed.error, false};
    }

    return doRequest(parsed.host, parsed.port, buildGetRequest(parsed, authToken), verifyCert);
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& body,
                              const std::string& contentType,
                              const std::string& authToken,
                              bool verifyCert) const {
    if (!wsaInitialized_ || !ctx_) {
        return {0, "", "HttpClient not initialized", false};
    }
    ERR_clear_error();

    ParsedUrl parsed = parseUrl(url);
    if (!parsed.error.empty()) {
        return {0, "", parsed.error, false};
    }

    // CRLF in any header value would allow injection (RFC 7230 §3.2).
    auto hasCrlf = [](const std::string& s) {
        return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
    };
    if (hasCrlf(parsed.path) || hasCrlf(contentType) || hasCrlf(authToken)) {
        return {0, "", "Invalid header value: CRLF in path, Content-Type, or Authorization", false};
    }

    return doRequest(parsed.host, parsed.port,
                     buildPostRequest(parsed, body, contentType, authToken), verifyCert);
}
