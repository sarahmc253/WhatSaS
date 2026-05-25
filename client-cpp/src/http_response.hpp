#ifndef HTTP_RESPONSE_HPP
#define HTTP_RESPONSE_HPP

#include <string>
#include "../include/HttpClient.hpp"

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;   // "443" default for https
    std::string path;   // "/foo/bar?q=1", always starts with '/'
    std::string error;  // non-empty if parseUrl rejected the URL
};

// Split a URL string into its components.
ParsedUrl    parseUrl(const std::string& url);

// Build a minimal HTTP/1.1 GET request string.
std::string  buildGetRequest(const ParsedUrl& u);

// Build a minimal HTTP/1.1 POST request string with a body.
std::string  buildPostRequest(const ParsedUrl& u,
                              const std::string& body,
                              const std::string& contentType,
                              const std::string& authToken = "");

// Parse a raw HTTP/1.1 response (headers + body) into HttpResponse.
HttpResponse parseResponse(const std::string& raw);

#endif // HTTP_RESPONSE_HPP
