#include "../include/Client.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
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
    const std::string jsonBody =
        "{\"sender_id\":\"" + jsonEscape(senderId_) + "\","
        "\"recipient_id\":\"" + jsonEscape(recipientId) + "\","
        "\"message_id\":\"" + jsonEscape(enc->messageId) + "\","
        "\"nonce\":\"" + enc->nonceB64 + "\","
        "\"ciphertext\":\"" + enc->ctB64 + "\","
        "\"kem_output\":\"\","
        "\"sender_ephemeral_pk\":\"\","
        "\"timestamp\":" + std::to_string(static_cast<long long>(enc->timestamp)) + "}";

    return http_.post(baseUrl_ + "/messages", jsonBody, "application/json", verifyCert_);
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

    // 2. Locate the "messages" JSON array
    const std::string& body = resp.body_;
    auto arrPos = body.find("\"messages\":[");
    if (arrPos == std::string::npos) {
        std::cerr << "[receiveMessages] missing 'messages' array\n";
        return -1;
    }
    std::size_t arrStart = body.find('[', arrPos) + 1;
    std::size_t arrEnd   = body.find(']', arrStart);
    if (arrEnd == std::string::npos) {
        std::cerr << "[receiveMessages] malformed messages array\n";
        return -1;
    }

    // 3. Brace-depth object splitter — each top-level {...} is one message object
    int successCount = 0;
    std::size_t pos = arrStart;
    while (pos < arrEnd) {
        while (pos < arrEnd && (body[pos] == ' ' || body[pos] == ',' ||
                                body[pos] == '\n' || body[pos] == '\r' ||
                                body[pos] == '\t')) ++pos;
        if (pos >= arrEnd || body[pos] != '{') break;

        int depth = 0;
        std::size_t objStart = pos;
        while (pos < arrEnd) {
            if      (body[pos] == '{') ++depth;
            else if (body[pos] == '}') { if (--depth == 0) { ++pos; break; } }
            ++pos;
        }
        std::string obj = body.substr(objStart, pos - objStart);

        // 4. Extract and validate fields — all data from untrusted server
        auto senderId    = parseJsonString(obj, "sender_id");
        auto recipientId = parseJsonString(obj, "recipient_id");
        auto messageId   = parseJsonString(obj, "message_id");
        auto nonceB64    = parseJsonString(obj, "nonce");
        auto ctB64       = parseJsonString(obj, "ciphertext");
        auto tsOpt       = parseJsonInt   (obj, "timestamp");

        if (!senderId || !recipientId || !messageId ||
            !nonceB64 || !ctB64 || !tsOpt) {
            std::cerr << "[receiveMessages] skipping object: missing field(s)\n";
            continue;
        }

        // message_id must be exactly 32 lowercase hex chars
        if (messageId->size() != 32 ||
            messageId->find_first_not_of("0123456789abcdef") != std::string::npos) {
            std::cerr << "[receiveMessages] invalid message_id format\n";
            continue;
        }

        // 5. Base64-decode nonce and ciphertext (returns empty on invalid input)
        std::vector<uint8_t> nonce = b64Decode(*nonceB64);
        std::vector<uint8_t> ct    = b64Decode(*ctB64);

        // 6. Construct Message — throws std::invalid_argument if nonce != 12 or ct < 16
        try {
            Message msg(*messageId, *senderId, *recipientId, ct, nonce,
                        static_cast<std::time_t>(*tsOpt));

            // 7. Store raw encrypted message, keyed by canonical peer
            store.addMessage(msg, peerKey(*senderId, *recipientId));

            // 8. Decrypt via message_crypto; pass result to Conversation
            auto dm = decryptMessage(aesKey_, msg);
            if (!dm) {
                std::cerr << "[receiveMessages] decryption failed: "
                          << *messageId << "\n";
                continue;
            }

            // 9. Only add to conv if this message belongs to that peer conversation.
            // The peer is whoever is not the local user (senderId_).
            const std::string& peer = (*senderId == senderId_) ? *recipientId : *senderId;
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

void printConversation(const Conversation& conv) {
    auto messages = conv.getMessages();
    if (messages.empty()) {
        std::cout << "(no messages)\n";
        return;
    }
    for (const auto& dm : messages) {
        char buf[20];
        buf[0] = '\0';
        std::time_t ts = dm.timestamp;
        if (std::tm* t = std::localtime(&ts)) {
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        }
        std::cout << "[" << buf << "] " << dm.senderId << ": " << dm.plaintext << "\n";
    }
}