#include "../src/tcp_connect.hpp"
#include <stdio.h>

int main() {
    printf("=== TCP Socket Connection Test (Phase A Part 2) ===\n\n");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("FAILED: WSAStartup\n");
        return 1;
    }
    printf("[PASS] WSAStartup\n");

    // Test 1: valid host should connect
    printf("\nTest 1: Connect to httpbin.org:443\n");
    std::string err;
    SOCKET sock = connectTcp("httpbin.org", "443", err);
    if (sock != INVALID_SOCKET) {
        printf("[PASS] Connected (socket=%llu)\n", (unsigned long long)sock);
        closesocket(sock);
    } else {
        printf("[FAIL] %s\n", err.c_str());
        WSACleanup();
        return 1;
    }

    // Test 2: invalid host should fail (not hang — timeout enforced)
    printf("\nTest 2: Connect to invalid host (expect failure within %ds)\n", CONNECT_TIMEOUT_SEC);
    SOCKET bad = connectTcp("invalid-host-that-does-not-exist-xyz.com", "443", err);
    if (bad == INVALID_SOCKET) {
        printf("[PASS] Correctly failed: %s\n", err.c_str());
    } else {
        printf("[FAIL] Unexpectedly connected to invalid host\n");
        closesocket(bad);
        WSACleanup();
        return 1;
    }

    // Test 3: repeated connections — socket leak check
    printf("\nTest 3: 5 repeated connections to httpbin.org:443\n");
    for (int i = 0; i < 5; i++) {
        SOCKET s = connectTcp("httpbin.org", "443", err);
        if (s != INVALID_SOCKET) {
            printf("  [%d] PASS\n", i + 1);
            closesocket(s);
        } else {
            printf("  [%d] FAIL: %s\n", i + 1, err.c_str());
            WSACleanup();
            return 1;
        }
    }

    WSACleanup();
    printf("\n=== All tests passed ===\n");
    return 0;
}
