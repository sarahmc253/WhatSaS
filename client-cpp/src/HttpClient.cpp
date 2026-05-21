#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>

#include "../include/HttpClient.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <iostream>
#include <sstream>
#include <cstring>

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

// Forward declarations for helper functions (Parts 2-4 implemented here)
static SOCKET connectTcp(const std::string& host,
                         const std::string& port,
                         std::string& errorOut);

// Windows-specific: Convert WSA error code to string
static std::string getWsaErrorString(int errorCode) {
    char buffer[256] = {0};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        sizeof(buffer) - 1,
        nullptr);
    return std::string(buffer);
}

// Helper: Convert hex string to size_t (for chunked encoding)
static std::size_t hexToSize(const std::string& hex) {
    return std::stoul(hex, nullptr, 16);
}

//Constructor / Destructor (WSA and SSL_CTX initialization)

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
    // ctx_ is automatically cleaned up by unique_ptr
    if (wsaInitialized_) {
        WSACleanup();
    }
}

// Part 2: Raw TCP Socket Connection using getaddrinfo, socket, connect

static SOCKET connectTcp(const std::string& host,
                         const std::string& port,
                         std::string& errorOut) {
    struct addrinfo hints;
    struct addrinfo* result = nullptr;
    struct addrinfo* p = nullptr;
    SOCKET connectSocket = INVALID_SOCKET;

    // Setup hints for getaddrinfo
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve host name to address
    int iResult = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (iResult != 0) {
        errorOut = "getaddrinfo failed: " + std::to_string(iResult);
        return INVALID_SOCKET;
    }

    // Try each result until we can connect
    for (p = result; p != nullptr; p = p->ai_next) {
        // Create a socket
        connectSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connectSocket == INVALID_SOCKET) {
            int wsaErr = WSAGetLastError();
            continue;  // Try next address
        }

        // Attempt to connect to the socket
        iResult = connect(connectSocket, p->ai_addr, (int)p->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(connectSocket);
            connectSocket = INVALID_SOCKET;
            continue;  // Try next address
        }

        // Successfully connected
        break;
    }

    freeaddrinfo(result);

    if (connectSocket == INVALID_SOCKET) {
        int wsaErr = WSAGetLastError();
        errorOut = "Could not connect to " + host + ":" + port + " - " +
                   getWsaErrorString(wsaErr);
        return INVALID_SOCKET;
    }

    return connectSocket;
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
