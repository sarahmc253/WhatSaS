#include "http_response.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>

// ============================================================================
// URL parser
// ============================================================================

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl out;
    std::string rest = url;

    auto schemeEnd = rest.find("://");
    if (schemeEnd != std::string::npos) {
        out.scheme = rest.substr(0, schemeEnd);
        rest = rest.substr(schemeEnd + 3);
    }

    // Bug fix: reject plain HTTP — this client only supports HTTPS
    if (out.scheme == "http") {
        out.error = "Plain HTTP not supported — use HTTPS";
        return out;
    }
    out.port = "443";

    auto slashPos = rest.find('/');
    std::string hostPort = (slashPos != std::string::npos) ? rest.substr(0, slashPos) : rest;
    out.path = (slashPos != std::string::npos) ? rest.substr(slashPos) : "/";

    // Explicit port override (e.g. host:8443)
    auto colonPos = hostPort.rfind(':');
    if (colonPos != std::string::npos) {
        out.host = hostPort.substr(0, colonPos);
        out.port = hostPort.substr(colonPos + 1);
    } else {
        out.host = hostPort;
    }

    return out;
}

// ============================================================================
// Request builder
// ============================================================================

std::string buildGetRequest(const ParsedUrl& u) {
    // RFC 7230: Host header must include port when it differs from the default (443)
    std::string hostHeader = (u.port == "443") ? u.host : u.host + ":" + u.port;
    return "GET " + u.path + " HTTP/1.1\r\n"
           "Host: " + hostHeader + "\r\n"
           "Connection: close\r\n"
           "Accept: */*\r\n"
           "\r\n";
}

std::string buildPostRequest(const ParsedUrl& u,
                             const std::string& body,
                             const std::string& contentType) {
    // Reject \r or \n in header-field values: an attacker-controlled path or
    // content-type containing CRLF would terminate the current header line and
    // inject arbitrary headers or body content (RFC 7230 §3.2 forbids obs-fold).
    auto hasCrlf = [](const std::string& s) {
        return s.find('\r') != std::string::npos || s.find('\n') != std::string::npos;
    };
    if (hasCrlf(u.path)) {
        // Returning an empty string is safe: HttpClient::doRequest will send it
        // and receive a 400, which surfaces as an error to the caller.
        // We cannot return HttpResponse here (wrong return type), so we use a
        // sentinel that will produce a parse error rather than silently sending
        // a malformed request. Callers validate the response ok_ flag.
        return "";
    }
    if (hasCrlf(contentType)) {
        return "";
    }

    std::string hostHeader = (u.port == "443") ? u.host : u.host + ":" + u.port;
    return "POST " + u.path + " HTTP/1.1\r\n"
           "Host: " + hostHeader + "\r\n"
           "Connection: close\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

// ============================================================================
// Response parser
// ============================================================================

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Decode a chunked transfer-encoding body.
// Each chunk: <hex-size>\r\n<data>\r\n — terminated by 0\r\n\r\n
static std::string decodeChunked(const std::string& body) {
    std::string out;
    std::size_t pos = 0;
    while (pos < body.size()) {
        auto crlf = body.find("\r\n", pos);
        if (crlf == std::string::npos) break;

        std::string sizeLine = body.substr(pos, crlf - pos);
        auto semi = sizeLine.find(';');  // strip chunk extensions
        if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);

        std::size_t chunkSize = 0;
        try { chunkSize = std::stoul(sizeLine, nullptr, 16); } catch (const std::exception&) { break; }
        if (chunkSize == 0) break;

        pos = crlf + 2;
        if (pos + chunkSize > body.size()) break;
        out.append(body, pos, chunkSize);
        pos += chunkSize + 2;  // skip trailing \r\n after chunk data
    }
    return out;
}

HttpResponse parseResponse(const std::string& raw) {
    HttpResponse resp;

    auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        resp.error_ = "Malformed response: no header/body separator";
        return resp;
    }

    std::string headers = raw.substr(0, headerEnd);
    std::string body    = raw.substr(headerEnd + 4);

    // Status line: "HTTP/1.1 200 OK"
    auto firstCrlf = headers.find("\r\n");
    std::string statusLine = headers.substr(0, firstCrlf);
    auto sp1 = statusLine.find(' ');
    auto sp2 = (sp1 != std::string::npos) ? statusLine.find(' ', sp1 + 1) : std::string::npos;
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        resp.error_ = "Malformed status line: " + statusLine;
        return resp;
    }
    try {
        resp.statusCode_ = std::stol(statusLine.substr(sp1 + 1, sp2 - sp1 - 1));
    } catch (const std::exception&) {
        resp.error_ = "Malformed status code in: " + statusLine;
        return resp;
    }

    // Scan headers for Transfer-Encoding and Content-Length
    std::string transferEncoding;
    long contentLength = -1;

    std::istringstream hstream(headers.substr(firstCrlf + 2));
    std::string line;
    while (std::getline(hstream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name  = toLower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        auto vstart = value.find_first_not_of(" \t");
        if (vstart != std::string::npos) value = value.substr(vstart);

        if (name == "transfer-encoding") {
            transferEncoding = value;
        } else if (name == "content-length") {
            try { contentLength = std::stol(value); } catch (const std::exception&) {}
        }
    }

    if (toLower(transferEncoding).find("chunked") != std::string::npos) {
        resp.body_ = decodeChunked(body);
    } else if (contentLength >= 0) {
        resp.body_ = body.substr(0, static_cast<std::size_t>(contentLength));
    } else {
        resp.body_ = body;
    }

    resp.ok_    = true;
    resp.error_ = "";
    return resp;
}
