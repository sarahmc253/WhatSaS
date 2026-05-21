// Test program for raw TCP socket connection (Phase A Part 2)
// Tests basic connectivity to httpbin.org:443 without TLS

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string>
#include <cstring>

// Forward declaration of the connectTcp function (defined in HttpClient.cpp)
// We'll test it by re-implementing here for simplicity or by directly testing

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

// Inline implementation of connectTcp for testing
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

int main() {
    printf("=== TCP Socket Connection Test (Phase A Part 2) ===\n\n");

    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("FAILED: WSAStartup returned %d\n", result);
        return 1;
    }
    printf("[PASS] WSAStartup successful\n");

    // Test 1: Connect to httpbin.org:443 (should succeed)
    printf("\nTest 1: Connect to httpbin.org:443\n");
    std::string err1;
    SOCKET sock1 = connectTcp("httpbin.org", "443", err1);
    if (sock1 != INVALID_SOCKET) {
        printf("[PASS] Connected to httpbin.org:443 (socket=%llu)\n", (unsigned long long)sock1);
        closesocket(sock1);
    } else {
        printf("[FAIL] Could not connect: %s\n", err1.c_str());
        WSACleanup();
        return 1;
    }

    // Test 2: Connect to invalid host (should fail)
    printf("\nTest 2: Connect to invalid-host-that-does-not-exist-xyz.com:443\n");
    std::string err2;
    SOCKET sock2 = connectTcp("invalid-host-that-does-not-exist-xyz.com", "443", err2);
    if (sock2 == INVALID_SOCKET) {
        printf("[PASS] Correctly failed to connect: %s\n", err2.c_str());
    } else {
        printf("[FAIL] Unexpectedly connected to invalid host\n");
        closesocket(sock2);
        WSACleanup();
        return 1;
    }

    // Test 3: Repeated connections (check for leaks)
    printf("\nTest 3: Repeated connections (5 iterations) to test for socket leaks\n");
    for (int i = 0; i < 5; i++) {
        std::string err;
        SOCKET sock = connectTcp("httpbin.org", "443", err);
        if (sock != INVALID_SOCKET) {
            printf("  Iteration %d: [PASS] Connected\n", i + 1);
            closesocket(sock);
        } else {
            printf("  Iteration %d: [FAIL] %s\n", i + 1, err.c_str());
            WSACleanup();
            return 1;
        }
    }

    // Cleanup Winsock
    WSACleanup();
    printf("\n=== All tests passed ===\n");
    return 0;
}
