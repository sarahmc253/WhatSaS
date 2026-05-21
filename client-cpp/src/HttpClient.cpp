#include "tcp_connect.hpp"
#include "tls_connect.hpp"
#include "http_response.hpp"
#include "../include/HttpClient.hpp"

#include <openssl/err.h>
#include <iostream>
#include <memory>

// RAII wrappers for OpenSSL SSL* and raw SOCKET

struct SslDeleter   { void operator()(SSL*    s) const { if (s) SSL_free(s); } };
struct SocketDeleter {
    void operator()(SOCKET* s) const {
        if (s && *s != INVALID_SOCKET) closesocket(*s);
        delete s;
    }
};
using UniqueSSL    = std::unique_ptr<SSL,    SslDeleter>;
using UniqueSocket = std::unique_ptr<SOCKET, SocketDeleter>;

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

// Public API

HttpResponse HttpClient::get(const std::string& url, bool verifyCert) const {
    if (!wsaInitialized_ || !ctx_) {
        return {0, "", "HttpClient not initialized", false};
    }

    ERR_clear_error();

    ParsedUrl parsed = parseUrl(url);
    if (!parsed.error.empty()) {
        return {0, "", parsed.error, false};
    }

    std::string err;
    SOCKET fd = connectTcp(parsed.host, parsed.port, err);
    if (fd == INVALID_SOCKET) {
        return {0, "", err, false};
    }
    UniqueSocket sock(new SOCKET(fd));

    SSL* sslRaw = performTlsHandshake(ctx_.get(), fd, parsed.host, verifyCert, err);
    if (!sslRaw) {
        return {0, "", err, false};
    }
    UniqueSSL ssl(sslRaw);

    std::string req = buildGetRequest(parsed);
    if (SSL_write(ssl.get(), req.data(), static_cast<int>(req.size()))
            != static_cast<int>(req.size())) {
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

    // n == 0 is a clean TLS close_notify — normal end of response
    // n < 0 is a real error — do not attempt to parse whatever we received
    if (n < 0) {
        int sslErr = SSL_get_error(ssl.get(), n);
        if (sslErr != SSL_ERROR_ZERO_RETURN) {
            unsigned long e = ERR_get_error();
            return {0, "", e ? ERR_error_string(e, nullptr) : "SSL_read error", false};
        }
    }

    return parseResponse(raw);
}
