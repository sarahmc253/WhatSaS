#include "../src/tcp_connect.hpp"
#include <stdio.h>
#include <string>

// Spins up a minimal TCP listener on localhost, returns the port (or 0 on failure)
static int startLocalListener(SOCKET& listenerOut) {
    listenerOut = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenerOut == INVALID_SOCKET) return 0;

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;  // let OS pick a free port

    if (bind(listenerOut, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenerOut);
        listenerOut = INVALID_SOCKET;
        return 0;
    }
    if (listen(listenerOut, 1) == SOCKET_ERROR) {
        closesocket(listenerOut);
        listenerOut = INVALID_SOCKET;
        return 0;
    }

    int len = sizeof(addr);
    getsockname(listenerOut, reinterpret_cast<sockaddr*>(&addr), &len);
    return ntohs(addr.sin_port);
}

int main(int argc, char* argv[]) {
    bool networkTests = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--network") networkTests = true;
    }

    printf("=== TCP Socket Connection Test ===\n");
    if (!networkTests) printf("(pass --network to also run external connectivity tests)\n");
    printf("\n");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("FAILED: WSAStartup\n");
        return 1;
    }
    printf("[PASS] WSAStartup\n");

    // Test 1: connect to a local listener — no internet required
    printf("\nTest 1: Connect to localhost listener\n");
    SOCKET listener = INVALID_SOCKET;
    int port = startLocalListener(listener);
    if (port == 0) {
        printf("[FAIL] Could not start local listener\n");
        WSACleanup();
        return 1;
    }
    std::string err;
    SOCKET sock = connectTcp("127.0.0.1", std::to_string(port), err);
    closesocket(listener);
    if (sock != INVALID_SOCKET) {
        printf("[PASS] Connected to 127.0.0.1:%d\n", port);
        closesocket(sock);
    } else {
        printf("[FAIL] %s\n", err.c_str());
        WSACleanup();
        return 1;
    }

    // Test 2: invalid host should fail cleanly within timeout
    printf("\nTest 2: Invalid host (expect failure within %ds)\n", CONNECT_TIMEOUT_SEC);
    SOCKET bad = connectTcp("invalid-host-that-does-not-exist-xyz.com", "443", err);
    if (bad == INVALID_SOCKET) {
        printf("[PASS] Correctly failed: %s\n", err.c_str());
    } else {
        printf("[FAIL] Unexpectedly connected to invalid host\n");
        closesocket(bad);
        WSACleanup();
        return 1;
    }

    // Test 3: refused connection (port 1 is almost certainly not listening)
    printf("\nTest 3: Refused connection to 127.0.0.1:1\n");
    SOCKET refused = connectTcp("127.0.0.1", "1", err);
    if (refused == INVALID_SOCKET) {
        printf("[PASS] Correctly refused: %s\n", err.c_str());
    } else {
        printf("[FAIL] Unexpectedly connected to port 1\n");
        closesocket(refused);
        WSACleanup();
        return 1;
    }

    // Optional: external connectivity (only with --network flag)
    if (networkTests) {
        printf("\nTest 4 [network]: Connect to sas.theburkenator.com:443\n");
        SOCKET ext = connectTcp("sas.theburkenator.com", "443", err);
        if (ext != INVALID_SOCKET) {
            printf("[PASS] Connected to sas.theburkenator.com:443\n");
            closesocket(ext);
        } else {
            printf("[FAIL] %s\n", err.c_str());
            WSACleanup();
            return 1;
        }
    }

    WSACleanup();
    printf("\n=== All tests passed ===\n");
    return 0;
}
