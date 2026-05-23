#ifndef CRYPTO_UTILS_HPP
#define CRYPTO_UTILS_HPP

#include <sodium.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

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

// Decode standard base64 (RFC 4648) into bytes.
// Returns empty vector if the input is not valid base64.
static inline std::vector<uint8_t> b64Decode(const std::string& encoded) {
    std::vector<uint8_t> out(encoded.size());
    std::size_t outLen = 0;
    if (sodium_base642bin(
            out.data(), out.size(),
            encoded.c_str(), encoded.size(),
            nullptr, &outLen, nullptr,
            sodium_base64_VARIANT_ORIGINAL) != 0) {
        return {};
    }
    out.resize(outLen);
    return out;
}

// Extract the string value for a given key from a flat JSON object.
// Accepts optional whitespace between ':' and the opening '"' (RFC 7159 §2).
// Returns nullopt on missing key, unterminated string, or unrecognised escape.
static inline std::optional<std::string> parseJsonString(
    const std::string& json, const std::string& key)
{
    // Match "key": (with optional whitespace before opening quote)
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();

    // Skip optional whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                  json[pos] == '\n' || json[pos] == '\r')) ++pos;

    // Expect opening quote
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;

    std::string result;
    bool closed = false;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') { closed = true; break; }
        if (c == '\\') {
            if (pos >= json.size()) return std::nullopt;
            char esc = json[pos++];
            switch (esc) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u':
                    if (pos + 4 > json.size()) return std::nullopt;
                    pos += 4;
                    break;
                default: return std::nullopt;
            }
        } else {
            result += c;
        }
    }
    // Unterminated string — never found a closing '"'
    if (!closed) return std::nullopt;
    return result;
}

// Extract an integer value for a given key from a flat JSON object.
// Returns nullopt on missing key or non-integer value.
static inline std::optional<long long> parseJsonInt(
    const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return std::nullopt;

    std::size_t start = pos;
    if (json[pos] == '-') ++pos;
    if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))
        return std::nullopt;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;

    try {
        return std::stoll(json.substr(start, pos - start));
    } catch (...) {
        return std::nullopt;
    }
}

// Generate a 32-char lowercase hex string from 16 CSPRNG bytes.
static inline std::string generateMsgId() {
    unsigned char raw[16];
    randombytes_buf(raw, sizeof(raw));
    // sodium_bin2hex writes 2*len + 1 bytes (null-terminated); allocate 33, resize to 32.
    std::string hex(33, '\0');
    sodium_bin2hex(&hex[0], 33, raw, sizeof(raw));
    hex.resize(32);
    return hex;
}

// Build canonical associated data JSON — no spaces, exact key order.
// Both sides must reconstruct this string identically for AEAD tag verification.
static inline std::string buildAd(const std::string& senderId,
                                   const std::string& recipientId,
                                   const std::string& msgId,
                                   std::time_t ts) {
    return "{\"sender_id\":\"" + jsonEscape(senderId) +
           "\",\"recipient_id\":\"" + jsonEscape(recipientId) +
           "\",\"message_id\":\"" + jsonEscape(msgId) +
           "\",\"timestamp\":" + std::to_string(static_cast<long long>(ts)) + "}";
}

// Encrypt plaintext with AES-256-GCM. A fresh 12-byte nonce is drawn from the
// CSPRNG on every call; it is prepended to the returned blob so the receiver
// has everything needed to decrypt.
//
// Returns nonce (12 bytes) || ciphertext+tag, or empty vector on failure.
// key — exactly 32 bytes; caller derives via HKDF (or passes a temp key for now).
// ad  — associated data bound into the AEAD tag; construct with buildAd().
static inline std::vector<uint8_t> encryptAes256Gcm(
    const std::vector<uint8_t>& key,
    const std::string& plaintext,
    const std::string& ad)
{
    if (key.size() != crypto_aead_aes256gcm_KEYBYTES) return {};

    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));  // fresh CSPRNG nonce every call

    std::vector<uint8_t> ct(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nullptr, nonce, key.data());
    if (rc != 0) return {};
    ct.resize(ctLen);

    std::vector<uint8_t> out;
    out.reserve(sizeof(nonce) + ct.size());
    out.insert(out.end(), nonce, nonce + sizeof(nonce));
    out.insert(out.end(), ct.begin(), ct.end());
    return out;
}

// Decrypt a blob produced by encryptAes256Gcm (nonce || ciphertext+tag).
// Returns nullopt on authentication failure or malformed input.
// Returns an empty vector on successful decryption of empty plaintext.
// key — same key used to encrypt; ad — must match exactly or decryption fails.
static inline std::optional<std::vector<uint8_t>> decryptAes256Gcm(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& noncePlusCt,
    const std::string& ad)
{
    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;  // 12
    constexpr std::size_t tagLen   = crypto_aead_aes256gcm_ABYTES;     // 16

    if (key.size() != crypto_aead_aes256gcm_KEYBYTES) return std::nullopt;
    if (noncePlusCt.size() < nonceLen + tagLen) return std::nullopt;

    const unsigned char* nonce = noncePlusCt.data();
    const unsigned char* ct    = noncePlusCt.data() + nonceLen;
    std::size_t ctLen = noncePlusCt.size() - nonceLen;

    std::vector<uint8_t> pt(ctLen);
    unsigned long long ptLen = 0;
    int rc = crypto_aead_aes256gcm_decrypt(
        pt.data(), &ptLen, nullptr,
        ct, ctLen,
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nonce, key.data());
    if (rc != 0) return std::nullopt;
    pt.resize(ptLen);
    return pt;
}

#endif // CRYPTO_UTILS_HPP
