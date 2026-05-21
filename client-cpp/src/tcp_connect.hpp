#ifndef TCP_CONNECT_HPP
#define TCP_CONNECT_HPP

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <cstring>

static const int CONNECT_TIMEOUT_SEC = 5;

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

// Establishes a TCP connection to host:port with a connect timeout.
// Returns a valid connected SOCKET or INVALID_SOCKET; populates errorOut on failure.
static SOCKET connectTcp(const std::string& host,
                         const std::string& port,
                         std::string& errorOut) {
    struct addrinfo hints;
    struct addrinfo* result = nullptr;
    struct addrinfo* p = nullptr;
    SOCKET connectSocket = INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int iResult = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (iResult != 0) {
        errorOut = "getaddrinfo failed: " + std::to_string(iResult);
        return INVALID_SOCKET;
    }

    int lastWsaErr = 0;
    for (p = result; p != nullptr; p = p->ai_next) {
        connectSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connectSocket == INVALID_SOCKET) {
            lastWsaErr = WSAGetLastError();
            continue;
        }

        // Switch to non-blocking so connect() returns immediately
        u_long nonBlocking = 1;
        ioctlsocket(connectSocket, FIONBIO, &nonBlocking);

        iResult = connect(connectSocket, p->ai_addr, (int)p->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            int err = WSAGetLastError();
            // WSAEWOULDBLOCK is expected for a non-blocking connect in progress
            if (err != WSAEWOULDBLOCK) {
                lastWsaErr = err;
                closesocket(connectSocket);
                connectSocket = INVALID_SOCKET;
                continue;
            }
        }

        // Wait for the socket to become writable (connect completed) or timeout
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(connectSocket, &writeSet);

        fd_set exceptSet;
        FD_ZERO(&exceptSet);
        FD_SET(connectSocket, &exceptSet);

        struct timeval tv;
        tv.tv_sec  = CONNECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        int ready = select(0, nullptr, &writeSet, &exceptSet, &tv);
        if (ready <= 0) {
            // 0 = timeout, -1 = select error
            lastWsaErr = (ready == 0) ? WSAETIMEDOUT : WSAGetLastError();
            closesocket(connectSocket);
            connectSocket = INVALID_SOCKET;
            continue;
        }

        // Check for async connect error via SO_ERROR
        int soErr = 0;
        int soErrLen = sizeof(soErr);
        if (getsockopt(connectSocket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&soErr), &soErrLen) == SOCKET_ERROR
            || soErr != 0) {
            lastWsaErr = (soErr != 0) ? soErr : WSAGetLastError();
            closesocket(connectSocket);
            connectSocket = INVALID_SOCKET;
            continue;
        }

        // Restore blocking mode for subsequent reads/writes
        u_long blocking = 0;
        ioctlsocket(connectSocket, FIONBIO, &blocking);

        break;
    }

    freeaddrinfo(result);

    if (connectSocket == INVALID_SOCKET) {
        errorOut = "Could not connect to " + host + ":" + port + " - " +
                   getWsaErrorString(lastWsaErr);
        return INVALID_SOCKET;
    }

    return connectSocket;
}

#endif // TCP_CONNECT_HPP
