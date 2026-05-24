#ifndef CRYPTO_UTILS_HPP
#define CRYPTO_UTILS_HPP

#include <sodium.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

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

// Build canonical associated data JSON — ordered keys, no spaces.
// Uses ordered_json to guarantee key insertion order is preserved on dump(),
// since both encrypt and decrypt must reconstruct byte-for-byte identical AD.
static inline std::string buildAd(const std::string& senderId,
                                   const std::string& recipientId,
                                   const std::string& msgId,
                                   std::time_t ts) {
    nlohmann::ordered_json ad;
    ad["sender_id"]    = senderId;
    ad["recipient_id"] = recipientId;
    ad["message_id"]   = msgId;
    ad["timestamp"]    = static_cast<long long>(ts);
    return ad.dump();
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
