#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <wincrypt.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include "tls_connect.hpp"

// Load Windows system root CA certificates into an OpenSSL X509_STORE.
// OpenSSL on Windows does not read the Windows cert store by default, so without
// this any server cert signed by a Windows-trusted CA (e.g. DigiCert) would fail.
static void loadWindowsCerts(SSL_CTX* ctx) {
    HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
    if (!hStore) return;

    X509_STORE* store = SSL_CTX_get_cert_store(ctx);
    PCCERT_CONTEXT pCtx = nullptr;
    while ((pCtx = CertEnumCertificatesInStore(hStore, pCtx)) != nullptr) {
        const unsigned char* der =
            reinterpret_cast<const unsigned char*>(pCtx->pbCertEncoded);
        X509* x509 = d2i_X509(nullptr, &der, static_cast<long>(pCtx->cbCertEncoded));
        if (x509) {
            X509_STORE_add_cert(store, x509);
            X509_free(x509);
        }
    }
    CertCloseStore(hStore, 0);
}

// Initialise a shared SSL_CTX suitable for HTTPS client use.
// Called once from HttpClient constructor; returned pointer is owned by the caller.
SSL_CTX* createSslCtx() {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return nullptr;

    // TLS 1.2 minimum — 1.0 and 1.1 are deprecated (RFC 8996)
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // Load CAs: OpenSSL built-in paths first, then Windows system store
    SSL_CTX_set_default_verify_paths(ctx);
    loadWindowsCerts(ctx);

    // Always request peer cert; SSL_connect fails if chain does not validate
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    return ctx;
}

SSL* performTlsHandshake(SSL_CTX* ctx,
                         SOCKET   fd,
                         const std::string& hostname,
                         bool               verifyCert,
                         std::string&       errorOut) {
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { errorOut = "SSL_new failed"; return nullptr; }

    if (SSL_set_fd(ssl, static_cast<int>(fd)) != 1) {
        SSL_free(ssl);
        errorOut = "SSL_set_fd failed";
        return nullptr;
    }

    // SNI extension: tells the server which certificate to send
    SSL_set_tlsext_host_name(ssl, hostname.c_str());

    if (verifyCert) {
        // Override ctx-level SSL_VERIFY_PEER on this SSL object — keeps verifyCert=false
        // working without mutating the shared SSL_CTX
        SSL_set_verify(ssl, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        // Hostname verification: CN/SAN in the cert must match hostname
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (SSL_set1_host(ssl, hostname.c_str()) != 1) {
            SSL_free(ssl);
            errorOut = "SSL_set1_host failed";
            return nullptr;
        }
    } else {
        // Disable cert verification for this connection only
        SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
    }

    if (SSL_connect(ssl) != 1) {
        unsigned long e = ERR_get_error();
        errorOut = e ? ERR_error_string(e, nullptr) : "SSL_connect failed";
        SSL_free(ssl);
        return nullptr;
    }

    // Belt-and-suspenders: verify result even though SSL_VERIFY_PEER already
    // aborts the handshake on failure — catches any edge cases in OpenSSL versions
    if (verifyCert && SSL_get_verify_result(ssl) != X509_V_OK) {
        errorOut = X509_verify_cert_error_string(SSL_get_verify_result(ssl));
        SSL_free(ssl);
        return nullptr;
    }

    return ssl;  // ownership transferred to caller
}
