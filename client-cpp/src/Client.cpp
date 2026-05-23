#include "../include/Client.hpp"
#include "crypto_utils.hpp"

#include <sodium.h>
#include <stdexcept>
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
      http_() {
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
}

HttpResponse Client::sendMessage(const std::string& recipientId,
                                 const std::string& plaintext) const {
    const std::string msgId = generateMsgId();
    const std::time_t ts    = std::time(nullptr);
    const std::string ad    = buildAd(senderId_, recipientId, msgId, ts);

    // encryptAes256Gcm draws a fresh CSPRNG nonce on every call and returns
    // nonce (12 bytes) || ciphertext+tag as a single blob.
    const std::vector<uint8_t> blob = encryptAes256Gcm(aesKey_, plaintext, ad);
    if (blob.empty()) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }

    // Split blob back into nonce and ciphertext for the JSON payload.
    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;  // 12
    const std::string nonceB64 = b64Encode(blob.data(), nonceLen);
    const std::string ctB64    = b64Encode(blob.data() + nonceLen, blob.size() - nonceLen);

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
