#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <memory>

// Forward declarations to avoid including OpenSSL headers in every translation unit
struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;

struct HttpResponse {
    long        statusCode_ = 0;
    std::string body_;
    std::string error_;
    bool        ok_ = false;
};

class HttpClient {
public:
    // pinnedCertPath — absolute path to a self-signed server cert to pin, or "" for CA-only
    explicit HttpClient(const std::string& pinnedCertPath = "");
    ~HttpClient();

    // Non-copyable
    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Movable — manual to prevent double WSACleanup (source must be zeroed)
    HttpClient(HttpClient&& other) noexcept;
    HttpClient& operator=(HttpClient&& other) noexcept;

    // Perform HTTP GET request over HTTPS with optional SSL cert verification
    HttpResponse get(const std::string& url,
                     const std::string& authToken = "",
                     bool verifyCert = true) const;

    // Perform HTTP POST request over HTTPS with a JSON (or other) body
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::string& contentType = "application/json",
                      const std::string& authToken = "",
                      bool verifyCert = true) const;

    // Perform HTTP POST with no body (e.g. /revoke, /flush)
    HttpResponse postNoBody(const std::string& url,
                            const std::string& authToken = "",
                            bool verifyCert = true) const;

    // Perform HTTP DELETE request
    HttpResponse del(const std::string& url,
                     const std::string& authToken = "",
                     bool verifyCert = true) const;

private:
    // Custom deleter for SSL_CTX (defined in .cpp file)
    struct SslCtxDeleter {
        void operator()(SSL_CTX* ctx) const;
    };

    // Shared TCP → TLS → send → read → parse pipeline used by get() and post()
    HttpResponse doRequest(const std::string& host,
                           const std::string& port,
                           const std::string& requestStr,
                           bool verifyCert) const;

    bool wsaInitialized_;
    std::unique_ptr<SSL_CTX, SslCtxDeleter> ctx_;
};

#endif // HTTP_CLIENT_HPP
