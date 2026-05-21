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
    HttpClient();
    ~HttpClient();

    // Non-copyable
    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Movable — manual to prevent double WSACleanup (source must be zeroed)
    HttpClient(HttpClient&& other) noexcept;
    HttpClient& operator=(HttpClient&& other) noexcept;

    // Perform HTTP GET request over HTTPS with optional SSL cert verification
    HttpResponse get(const std::string& url, bool verifyCert = true) const;

private:
    // Custom deleter for SSL_CTX (defined in .cpp file)
    struct SslCtxDeleter {
        void operator()(SSL_CTX* ctx) const;
    };

    bool wsaInitialized_;
    std::unique_ptr<SSL_CTX, SslCtxDeleter> ctx_;
};

#endif // HTTP_CLIENT_HPP
