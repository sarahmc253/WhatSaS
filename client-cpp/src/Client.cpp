#include "../include/Client.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>

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
    auto enc = encryptMessage(aesKey_, senderId_, recipientId, plaintext);
    if (!enc) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }

    // kem_output and sender_ephemeral_pk are HPKE fields; empty until HPKE is added.
    nlohmann::json body;
    body["sender_id"]           = senderId_;
    body["recipient_id"]        = recipientId;
    body["message_id"]          = enc->messageId;
    body["nonce"]               = enc->nonceB64;
    body["ciphertext"]          = enc->ctB64;
    body["kem_output"]          = "";
    body["sender_ephemeral_pk"] = "";
    body["timestamp"]           = static_cast<long long>(enc->timestamp);

    return http_.post(baseUrl_ + "/messages", body.dump(), "application/json", verifyCert_);
}

HttpResponse Client::getMessages() const {
    return http_.get(baseUrl_ + "/messages", verifyCert_);
}

int Client::receiveMessages(MessageStore& store, Conversation& conv) const {
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
        long long   ts          = obj["timestamp"];

        // 4. Reject messages where local user is not a participant
        if (senderId != senderId_ && recipientId != senderId_) {
            std::cerr << "[receiveMessages] skipping: local user not a participant: "
                      << messageId << "\n";
            continue;
        }

        // 5. message_id must be exactly 32 lowercase hex chars
        if (messageId.size() != 32 ||
            messageId.find_first_not_of("0123456789abcdef") != std::string::npos) {
            std::cerr << "[receiveMessages] invalid message_id format\n";
            continue;
        }

        // 6. Base64-decode nonce and ciphertext (returns empty on invalid input)
        std::vector<uint8_t> nonce = b64Decode(nonceB64);
        std::vector<uint8_t> ct    = b64Decode(ctB64);

        // 7. Construct Message — throws std::invalid_argument if nonce != 12 or ct < 16
        try {
            Message msg(messageId, senderId, recipientId, ct, nonce,
                        static_cast<std::time_t>(ts));

            // 8. Store raw encrypted message, keyed by canonical peer
            store.addMessage(msg, peerKey(senderId, recipientId));

            // 9. Decrypt via message_crypto; pass result to Conversation
            auto dm = decryptMessage(aesKey_, msg);
            if (!dm) {
                std::cerr << "[receiveMessages] decryption failed: "
                          << messageId << "\n";
                continue;
            }

            // 10. Only add to conv if this message belongs to that peer conversation.
            // The peer is whoever is not the local user (senderId_).
            // Assumes user IDs use consistent casing (bob vs BOB) since peer comparison here is case sensitive.
            const std::string& peer = (senderId == senderId_) ? recipientId : senderId;
            if (peer == conv.getPeerId()) {
                conv.addMessage(std::move(*dm));
            }
            ++successCount;
        } catch (const std::invalid_argument& e) {
            std::cerr << "[receiveMessages] invalid message fields: "
                      << e.what() << "\n";
        }
    }
    return successCount;
}

