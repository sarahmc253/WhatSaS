#include "../include/Client.hpp"

#include <sodium.h>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

// Helpers

// Escape a string for use as a JSON string value.
// Escapes \, ", and the JSON control characters (\b \f \n \r \t and other < 0x20).
// The caller is responsible for wrapping the result in double-quotes.
static std::string jsonEscape(const std::string& s) {
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
                    // Control characters must be \uXXXX-escaped in JSON (RFC 8259 §7)
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

// Encode binary data as standard base64 (RFC 4648, no URL-safe variant)
// sodium_bin2base64 writes a null-terminated string; we strip the terminator
static std::string b64Encode(const unsigned char* data, std::size_t len) {
    std::size_t bufLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(bufLen, '\0');
    sodium_bin2base64(&out[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Generate a lowercase hex message ID from 16 CSPRNG bytes (128-bit collision resistance)
static std::string generateMsgId() {
    unsigned char raw[16];
    randombytes_buf(raw, sizeof(raw));
    std::string hex(32, '\0');
    sodium_bin2hex(&hex[0], 33, raw, sizeof(raw));
    hex.resize(32);
    return hex;
}

// Build canonical associated data JSON — no spaces, exact key order.
// Both sender and recipient must reconstruct this string identically for AEAD verification.
// All string values are JSON-escaped so a malicious senderId cannot alter the structure.
static std::string buildAd(const std::string& senderId,
                            const std::string& recipientId,
                            const std::string& msgId,
                            std::time_t ts) {
    return "{\"sender_id\":\"" + jsonEscape(senderId) +
           "\",\"recipient_id\":\"" + jsonEscape(recipientId) +
           "\",\"message_id\":\"" + jsonEscape(msgId) +
           "\",\"timestamp\":" + std::to_string(static_cast<long long>(ts)) + "}";
}

// Client

Client::Client(const std::string& baseUrl,
               const std::string& senderId,
               std::vector<uint8_t> aesKey,
               bool verifyCert)
    : baseUrl_(baseUrl),
      senderId_(senderId),
      aesKey_(std::move(aesKey)),
      verifyCert_(verifyCert),
      http_() {
    if (aesKey_.size() != crypto_aead_aes256gcm_KEYBYTES) {
        throw std::invalid_argument(
            "AES-256-GCM key must be exactly 32 bytes, got " +
            std::to_string(aesKey_.size()));
    }
    // sodium_init must run before any other libsodium call, including
    // crypto_aead_aes256gcm_is_available(). Calling libsodium functions before
    // init produces undefined behaviour.
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialisation failed");
    }
    // AES-256-GCM in libsodium requires hardware AES-NI; fail fast rather than
    // silently producing incorrect results on unsupported hardware.
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw std::runtime_error(
            "AES-256-GCM unavailable: hardware AES acceleration (AES-NI) required");
    }
}

HttpResponse Client::sendMessage(const std::string& recipientId,
                                 const std::string& plaintext) const {
    //Generate a fresh 12-byte nonce from CSPRNG not reusing a nonce with the same key.
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    //Stable message ID and timestamp for AEAD associated data
    const std::string msgId = generateMsgId();
    const std::time_t ts    = std::time(nullptr);

    // Canonical JSON associated data string for AEAD integrity/authentication
    const std::string ad = buildAd(senderId_, recipientId, msgId, ts);

    //Encrypt: ciphertext = plaintext || 16-byte AEAD auth tag
    std::vector<uint8_t> ct(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nullptr,     // nsec: unused by this construction
        nonce,
        aesKey_.data());
    if (rc != 0) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }
    ct.resize(ctLen);

    //base64-encode ciphertext and nonce for JSON transport
    const std::string ctB64    = b64Encode(ct.data(), ct.size());
    const std::string nonceB64 = b64Encode(nonce, sizeof(nonce));

    // Build JSON body. All string values are escaped to prevent JSON injection.
    // nonce/ciphertext are base64 and safe by construction; senderId/recipientId/msgId
    // are escaped because they originate from user input or client-generated hex.
    // kem_output and sender_ephemeral_pk are HPKE fields; empty until HPKE is added.
    const std::string jsonBody =
        "{\"sender_id\":\"" + jsonEscape(senderId_) + "\","
        "\"recipient_id\":\"" + jsonEscape(recipientId) + "\","
        "\"message_id\":\"" + jsonEscape(msgId) + "\","
        "\"nonce\":\"" + nonceB64 + "\","
        "\"ciphertext\":\"" + ctB64 + "\","
        "\"kem_output\":\"\","
        "\"sender_ephemeral_pk\":\"\","
        "\"timestamp\":" + std::to_string(static_cast<long long>(ts)) + "}";

    return http_.post(baseUrl_ + "/messages", jsonBody, "application/json", verifyCert_);
}

HttpResponse Client::getMessages() const {
    return http_.get(baseUrl_ + "/messages", verifyCert_);
}
