#include "../include/Client.hpp"
#include "crypto_utils.hpp"

#include <sodium.h>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

Client::Client(const std::string& baseUrl,
               const std::string& senderId,
               std::vector<uint8_t> aesKey,
               bool verifyCert)
    : baseUrl_(baseUrl),
      senderId_(senderId),
      aesKey_(std::move(aesKey)),
      verifyCert_(verifyCert),
      http_(),
      nonceCounter_(0) {
    if (aesKey_.size() != crypto_aead_aes256gcm_KEYBYTES) {
        throw std::invalid_argument(
            "AES-256-GCM key must be exactly 32 bytes, got " +
            std::to_string(aesKey_.size()));
    }
    // sodium_init must precede all other libsodium calls, including
    // crypto_aead_aes256gcm_is_available().
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialisation failed");
    }
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw std::runtime_error(
            "AES-256-GCM unavailable: hardware AES-NI required");
    }
    // Random base nonce, XOR'd with a per-message counter to guarantee uniqueness
    // under the same key. Purely random 96-bit nonces have a 50% collision
    // probability after 2^48 messages (birthday bound); the counter eliminates this.
    randombytes_buf(nonceBase_, sizeof(nonceBase_));
}

HttpResponse Client::sendMessage(const std::string& recipientId,
                                 const std::string& plaintext) const {
    // Derive nonce: XOR the random base with an 8-byte little-endian counter.
    // Counter occupies the first 8 bytes; upper 4 bytes remain from the base.
    unsigned char nonce[12];
    std::memcpy(nonce, nonceBase_, 12);
    uint64_t count = nonceCounter_.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < 8; ++i) {
        nonce[i] ^= static_cast<unsigned char>((count >> (8 * i)) & 0xFF);
    }

    const std::string msgId = generateMsgId();
    const std::time_t ts    = std::time(nullptr);
    const std::string ad    = buildAd(senderId_, recipientId, msgId, ts);

    std::vector<uint8_t> ct(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nullptr, nonce, aesKey_.data());
    if (rc != 0) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }
    ct.resize(ctLen);

    const std::string ctB64    = b64Encode(ct.data(), ct.size());
    const std::string nonceB64 = b64Encode(nonce, sizeof(nonce));

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
