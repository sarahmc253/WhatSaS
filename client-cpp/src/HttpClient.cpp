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

// Phase B stub (Parts 3-4): TLS and HTTP

HttpResponse HttpClient::get(const std::string& url, bool verifyCert) const {
    // Phase A: Stub implementation
    // This will be fully implemented in later
    HttpResponse resp;
    resp.statusCode_ = 0;
    resp.body_ = "";
    resp.error_ = "TLS layer not yet implemented (Phase B)";
    resp.ok_ = false;
    return resp;
}
