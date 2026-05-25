#include "../include/Client.hpp"
#include "hpke_utils.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
#include <nlohmann/json.hpp>
#include <limits>
#include <stdexcept>
#include <iostream>

Client::Client(const std::string& baseUrl,
               const std::string& senderId,
               std::vector<uint8_t> staticSk,
               std::vector<uint8_t> staticPk,
               bool verifyCert)
    : baseUrl_(baseUrl),
      senderId_(senderId),
      staticSk_(std::move(staticSk)),
      staticPk_(std::move(staticPk)),
      verifyCert_(verifyCert),
      http_() {
    if (staticSk_.size() != 32) {
        throw std::invalid_argument(
            "X25519 private key must be exactly 32 bytes, got " +
            std::to_string(staticSk_.size()));
    }
    if (staticPk_.size() != 32) {
        throw std::invalid_argument(
            "X25519 public key must be exactly 32 bytes, got " +
            std::to_string(staticPk_.size()));
    }
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialisation failed");
    }
    if (crypto_aead_aes256gcm_is_available() == 0) {
        throw std::runtime_error(
            "AES-256-GCM unavailable: hardware AES-NI required");
    }
}

HttpResponse Client::sendMessage(const std::string& recipientId,
                                 const std::vector<uint8_t>& recipientPk,
                                 const std::string& plaintext) const {
    // 1. DHKEM: derive per-message AES key + ephemeral pk.
    auto hpkeResult = hpkeSend(staticSk_, recipientPk);
    if (!hpkeResult) {
        return {0, "", "HPKE key derivation failed (low-order point or bad key)", false};
    }

    // 2. Encrypt with derived AES-256-GCM key.
    auto enc = encryptMessage(hpkeResult->aesKey, senderId_, recipientId, plaintext);
    sodium_memzero(hpkeResult->aesKey.data(), hpkeResult->aesKey.size());
    if (!enc) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }

    // 3. Build wire body — kem_output carries the ephemeral public key.
    nlohmann::json body;
    body["sender_id"]           = senderId_;
    body["recipient_id"]        = recipientId;
    body["message_id"]          = enc->messageId;
    body["nonce"]               = enc->nonceB64;
    body["ciphertext"]          = enc->ctB64;
    body["kem_output"]          = b64Encode(hpkeResult->ephPk.data(), hpkeResult->ephPk.size());
    body["sender_ephemeral_pk"] = "";  // deprecated field kept for server schema compat
    body["timestamp"]           = static_cast<long long>(enc->timestamp);

    return http_.post(baseUrl_ + "/messages", body.dump(), "application/json", verifyCert_);
}

HttpResponse Client::getMessages() const {
    return http_.get(baseUrl_ + "/messages", verifyCert_);
}

int Client::receiveMessages(MessageStore& store,
                            Conversation& conv,
                            const std::vector<uint8_t>& senderPk) const {
    // 1. HTTP fetch
    HttpResponse resp = getMessages();
    if (!resp.ok_ || resp.statusCode_ != 200 || resp.body_.empty()) {
        std::cerr << "[receiveMessages] HTTP error " << resp.statusCode_
                  << ": " << resp.error_ << "\n";
        return -1;
    }

    // 2. Parse entire response body as JSON
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        std::cerr << "[receiveMessages] JSON parse error\n";
        return -1;
    }

    if (!parsed.contains("messages") || !parsed["messages"].is_array()) {
        std::cerr << "[receiveMessages] missing or invalid 'messages' array\n";
        return -1;
    }

    // 3. Iterate message objects — all fields from untrusted server
    int successCount = 0;
    for (const auto& obj : parsed["messages"]) {
        if (!obj.contains("sender_id")    || !obj["sender_id"].is_string()         ||
            !obj.contains("recipient_id") || !obj["recipient_id"].is_string()      ||
            !obj.contains("message_id")   || !obj["message_id"].is_string()        ||
            !obj.contains("nonce")        || !obj["nonce"].is_string()             ||
            !obj.contains("ciphertext")   || !obj["ciphertext"].is_string()        ||
            !obj.contains("timestamp")    || !obj["timestamp"].is_number_integer()) {
            std::cerr << "[receiveMessages] skipping: missing or wrong-type field\n";
            continue;
        }

        std::string senderId    = obj["sender_id"];
        std::string recipientId = obj["recipient_id"];
        std::string messageId   = obj["message_id"];
        std::string nonceB64    = obj["nonce"];
        std::string ctB64       = obj["ciphertext"];

        long long ts;
        try {
            ts = obj["timestamp"].get<long long>();
        } catch (const nlohmann::json::exception&) {
            std::cerr << "[receiveMessages] skipping: timestamp unrepresentable as int64\n";
            continue;
        }

        if (ts < 0 || ts > static_cast<long long>(std::numeric_limits<std::time_t>::max())) {
            std::cerr << "[receiveMessages] skipping: timestamp out of range: " << ts << "\n";
            continue;
        }

        if (senderId != senderId_ && recipientId != senderId_) {
            std::cerr << "[receiveMessages] skipping: local user not a participant: "
                      << messageId << "\n";
            continue;
        }

        if (messageId.size() != 32 ||
            messageId.find_first_not_of("0123456789abcdef") != std::string::npos) {
            std::cerr << "[receiveMessages] invalid message_id format\n";
            continue;
        }

        // 4. Extract and validate kem_output (ephemeral public key from sender).
        if (!obj.contains("kem_output") || !obj["kem_output"].is_string()) {
            std::cerr << "[receiveMessages] skipping: missing kem_output field: "
                      << messageId << "\n";
            continue;
        }
        std::vector<uint8_t> ephPk = b64Decode(obj["kem_output"].get<std::string>());
        if (ephPk.size() != 32) {
            std::cerr << "[receiveMessages] skipping: kem_output must decode to 32 bytes: "
                      << messageId << "\n";
            continue;
        }

        // 5. Re-derive the per-message AES key via DHKEM.
        std::vector<uint8_t> aesKey = hpkeReceive(staticSk_, ephPk, senderPk);
        if (aesKey.empty()) {
            std::cerr << "[receiveMessages] HPKE receive failed (auth failure or low-order point): "
                      << messageId << "\n";
            continue;
        }

        // 6. Base64-decode nonce and ciphertext.
        std::vector<uint8_t> nonce = b64Decode(nonceB64);
        std::vector<uint8_t> ct    = b64Decode(ctB64);

        try {
            Message msg(messageId, senderId, recipientId, ct, nonce,
                        static_cast<std::time_t>(ts));

            store.addMessage(msg, peerKey(senderId, recipientId));

            auto dm = decryptMessage(aesKey, msg);
            sodium_memzero(aesKey.data(), aesKey.size());
            if (!dm) {
                std::cerr << "[receiveMessages] decryption failed: " << messageId << "\n";
                continue;
            }

            const std::string& peer = (senderId == senderId_) ? recipientId : senderId;
            if (peer == conv.getPeerId()) {
                conv.addMessage(std::move(*dm));
            }
            ++successCount;
        } catch (const std::invalid_argument& e) {
            sodium_memzero(aesKey.data(), aesKey.size());
            std::cerr << "[receiveMessages] invalid message fields: " << e.what() << "\n";
        }
    }
    return successCount;
}

std::vector<uint8_t> Client::fetchPeerPublicKey(const std::string& userId) const {
    HttpResponse resp = http_.get(baseUrl_ + "/users/" + userId + "/key", verifyCert_);
    if (!resp.ok_ || resp.statusCode_ != 200 || resp.body_.empty()) {
        std::cerr << "[fetchPeerPublicKey] HTTP error for " << userId
                  << ": " << resp.error_ << "\n";
        return {};
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        std::cerr << "[fetchPeerPublicKey] JSON parse error\n";
        return {};
    }

    if (!parsed.contains("public_key") || !parsed["public_key"].is_string()) {
        std::cerr << "[fetchPeerPublicKey] missing public_key field\n";
        return {};
    }

    std::vector<uint8_t> pk = b64Decode(parsed["public_key"].get<std::string>());
    if (pk.size() != 32) {
        std::cerr << "[fetchPeerPublicKey] public_key must be 32 bytes, got "
                  << pk.size() << "\n";
        return {};
    }

    // TOFU pinning: pin on first fetch; reject if key changes later.
    auto it = pinnedKeys_.find(userId);
    if (it == pinnedKeys_.end()) {
        pinnedKeys_[userId] = pk;
        std::cerr << "[fetchPeerPublicKey] TOFU: pinned public key for " << userId << "\n";
    } else if (it->second != pk) {
        std::cerr << "[fetchPeerPublicKey] WARNING: public key for " << userId
                  << " differs from pinned key — possible key substitution attack. Rejecting.\n";
        return {};
    }

    return pk;
}

HttpResponse Client::publishPublicKey(const std::string& userId,
                                      const std::vector<uint8_t>& publicKey) const {
    nlohmann::json body;
    body["user_id"]    = userId;
    body["public_key"] = b64Encode(publicKey.data(), publicKey.size());
    return http_.post(baseUrl_ + "/keys", body.dump(), "application/json", verifyCert_);
}
