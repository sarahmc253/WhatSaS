#ifndef TLS_CONNECT_HPP
#define TLS_CONNECT_HPP

// Must be included before any Windows headers in translation units that use this header.
// tcp_connect.hpp already pulls in winsock2.h, so include order is handled there.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <openssl/ssl.h>
#include <string>

// Create and configure a client SSL_CTX:
//   - TLS 1.2 minimum
//   - Windows system CA store loaded
//   - SSL_VERIFY_PEER enabled
// Ownership of the returned pointer is transferred to the caller.
SSL_CTX* createSslCtx();

// Perform a TLS handshake on an already-connected TCP socket fd.
// - SNI set to hostname (required for virtual-hosted servers)
// - Hostname verification enabled (CN/SAN must match)
// - verifyCert=false disables cert chain validation (testing only)
// Returns SSL* with ownership transferred to caller, or nullptr on failure.
SSL* performTlsHandshake(SSL_CTX*           ctx,
                         SOCKET             fd,
                         const std::string& hostname,
                         bool               verifyCert,
                         std::string&       errorOut);

#endif // TLS_CONNECT_HPP
