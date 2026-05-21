#ifndef CRYPTO_UTILS_HPP
#define CRYPTO_UTILS_HPP

#include <sodium.h>
#include <cstdio>
#include <cstring>
#include <string>

// Escape a string for use as a JSON string value (RFC 8259 §7).
// Caller wraps the result in double-quotes.
static inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Encode binary data as standard base64 (RFC 4648).
static inline std::string b64Encode(const unsigned char* data, std::size_t len) {
    std::size_t bufLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(bufLen, '\0');
    sodium_bin2base64(&out[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Generate a 32-char lowercase hex string from 16 CSPRNG bytes.
static inline std::string generateMsgId() {
    unsigned char raw[16];
    randombytes_buf(raw, sizeof(raw));
    std::string hex(32, '\0');
    sodium_bin2hex(&hex[0], 33, raw, sizeof(raw));
    hex.resize(32);
    return hex;
}

#endif // CRYPTO_UTILS_HPP
