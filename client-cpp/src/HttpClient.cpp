#include "tcp_connect.hpp"   // connectTcp + getWsaErrorString (shared with test_tcp)

#include <wincrypt.h>
#include "../include/HttpClient.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <iostream>
#include <sstream>

// RAII wrapper for SOCKET
struct SocketDeleter {
    void operator()(SOCKET* s) const {
        if (s && *s != INVALID_SOCKET) {
            closesocket(*s);
        }
        delete s;
    }
};
using UniqueSocket = std::unique_ptr<SOCKET, SocketDeleter>;

// RAII wrapper for SSL_CTX — implementation of the deleter
void HttpClient::SslCtxDeleter::operator()(SSL_CTX* ctx) const {
    if (ctx) {
        SSL_CTX_free(ctx);
    }
}

// Helper: Convert hex string to size_t (for chunked encoding)
static std::size_t hexToSize(const std::string& hex) {
    return std::stoul(hex, nullptr, 16);
}

// Constructor / Destructor (WSA and SSL_CTX initialization)

HttpClient::HttpClient() : wsaInitialized_(false), ctx_(nullptr) {
    // Initialize Winsock2
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result == 0) {
        wsaInitialized_ = true;
    } else {
        std::cerr << "WSAStartup failed: " << getWsaErrorString(result) << "\n";
    }

    // Initialize OpenSSL context (full init deferred to Phase B)
    // For Phase A, we only initialize the structure; TLS setup happens in Phase B
}

HttpClient::~HttpClient() {
    if (wsaInitialized_) {
        WSACleanup();
    }
}

HttpClient::HttpClient(HttpClient&& other) noexcept
    : wsaInitialized_(other.wsaInitialized_), ctx_(std::move(other.ctx_)) {
    other.wsaInitialized_ = false;  // prevent double WSACleanup
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

HttpResponse HttpClient::get(const std::string& url, bool verifyCert) const {
    if (!wsaInitialized_) {
        return {0, "", "Winsock not initialized", false};
    }

    // Phase A: verify TCP reachability; TLS + HTTP parsing added in Phase B
    // For now, extract host and attempt a raw TCP connect to validate the path
    // A URL like "https://host/path" → host="host", port="443"
    std::string host;
    std::string port = "443";

    // Strip scheme
    std::string rest = url;
    auto schemeEnd = rest.find("://");
    if (schemeEnd != std::string::npos) {
        rest = rest.substr(schemeEnd + 3);
    }

    // Strip path
    auto slashPos = rest.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;

    // Split host:port
    auto colonPos = hostPort.rfind(':');
    if (colonPos != std::string::npos) {
        host = hostPort.substr(0, colonPos);
        port = hostPort.substr(colonPos + 1);
    } else {
        host = hostPort;
    }

    std::string err;
    SOCKET fd = connectTcp(host, port, err);
    if (fd == INVALID_SOCKET) {
        return {0, "", err, false};
    }
    closesocket(fd);

    // TCP connected — TLS handshake and HTTP exchange implemented in Phase B
    return {0, "", "TLS not yet implemented (Phase B)", false};
}
