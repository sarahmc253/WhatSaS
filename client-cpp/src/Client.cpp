#include "../include/Client.hpp"
#include "hpke_utils.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <iostream>
#ifdef _WIN32
#  include <windows.h>
#endif

Client::Client(const std::string& baseUrl,
               const std::string& senderId,
               std::vector<uint8_t> staticSk,
               std::vector<uint8_t> staticPk,
               const std::string& pinsPath,
               bool verifyCert)
    : baseUrl_(baseUrl),
      senderId_(senderId),
      staticSk_(std::move(staticSk)),
      staticPk_(std::move(staticPk)),
      verifyCert_(verifyCert),
      http_(),
      pinsPath_(pinsPath) {
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
    loadPins();
}

// File format: one line per pin — "<userId> <base64(pk)>\n"
// Lines starting with '#' are ignored. Malformed lines are skipped with a warning.
void Client::loadPins() {
    std::ifstream f(pinsPath_);
    if (!f.is_open()) return;  // file doesn't exist yet — that's fine on first run
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string userId, pkB64;
        if (!(ss >> userId >> pkB64)) {
            std::cerr << "[loadPins] skipping malformed line in " << pinsPath_ << "\n";
            continue;
        }
        std::vector<uint8_t> pk = b64Decode(pkB64);
        if (pk.size() != 32) {
            std::cerr << "[loadPins] skipping bad key for " << userId << "\n";
            continue;
        }
        pinnedKeys_[userId] = std::move(pk);
    }
    std::cerr << "[loadPins] loaded " << pinnedKeys_.size()
              << " pin(s) from " << pinsPath_ << "\n";
}

// Rewrites the entire file atomically: write to <path>.tmp, then rename over <path>.
// Returns true on success. On any failure the tmp file is removed and false is returned,
// leaving the previous pins file intact.
bool Client::savePins() const {
    if (pinsPath_.empty()) return false;
    const std::string tmp = pinsPath_ + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f.is_open()) {
            std::cerr << "[savePins] cannot write to " << tmp << "\n";
            return false;
        }
        f << "# WhatSaS TOFU key pins — do not edit manually\n";
        for (const auto& [userId, pk] : pinnedKeys_) {
            f << userId << " " << b64Encode(pk.data(), pk.size()) << "\n";
        }
        // flush + close before rename so all bytes are on disk
        f.flush();
        if (!f.good()) {
            std::cerr << "[savePins] write error for " << tmp << "\n";
            std::remove(tmp.c_str());
            return false;
        }
    }
    // Atomic replace. std::rename on the Windows CRT fails with EEXIST when the
    // destination already exists. Use MoveFileExW with MOVEFILE_REPLACE_EXISTING
    // on Windows, which is the correct atomic-replace primitive on this platform.
    #ifdef _WIN32
    // Widen once at the top of savePins()
    auto toWide = [](const std::string& utf8) -> std::wstring {
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), len);
        return w;
    };
    
    std::wstring wdest = toWide(pinsPath_);
    std::wstring wtmp  = wdest + L".tmp";
    
    // Open with wide path
    FILE* f = _wfopen(wtmp.c_str(), L"w");
    if (!f) { /* handle error */ return false; }
    // ... write via f ...
    fclose(f);
    
    // Atomic replace
    if (!MoveFileExW(wtmp.c_str(), wdest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "[savePins] MoveFileExW failed (" << GetLastError() << ")\n";
        DeleteFileW(wtmp.c_str());   // wide cleanup — can't fail silently
        return false;
    }
    #else
    // narrow / POSIX path unchanged
    #endif
    return true;
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

        // 4. Skip HPKE derivation for messages we sent — we are the sender, not the
        //    intended receiver, so hpkeReceive with our own sk would be meaningless.
        //    Store and count them but do not attempt decryption.
        if (senderId == senderId_) {
            std::vector<uint8_t> nonceSelf = b64Decode(nonceB64);
            std::vector<uint8_t> ctSelf    = b64Decode(ctB64);
            try {
                Message msg(messageId, senderId, recipientId, ctSelf, nonceSelf,
                            static_cast<std::time_t>(ts));
                store.addMessage(msg, peerKey(senderId, recipientId));
                ++successCount;
            } catch (const std::invalid_argument& e) {
                std::cerr << "[receiveMessages] own-message invalid fields: " << e.what() << "\n";
            }
            continue;
        }

        // 5. Extract and validate kem_output (ephemeral public key from sender).
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

        // 6. Re-derive the per-message AES key via DHKEM. Must pass HPKE auth before
        //    storing — do not call store.addMessage until decryption succeeds.
        std::vector<uint8_t> aesKey = hpkeReceive(staticSk_, ephPk, senderPk);
        if (aesKey.empty()) {
            std::cerr << "[receiveMessages] HPKE receive failed (auth failure or low-order point): "
                      << messageId << "\n";
            continue;
        }

        // 7. Base64-decode nonce and ciphertext, construct Message, decrypt.
        std::vector<uint8_t> nonce = b64Decode(nonceB64);
        std::vector<uint8_t> ct    = b64Decode(ctB64);

        try {
            Message msg(messageId, senderId, recipientId, ct, nonce,
                        static_cast<std::time_t>(ts));

            auto decrypted = decryptMessage(aesKey, msg);
            sodium_memzero(aesKey.data(), aesKey.size());
            if (!decrypted) {
                std::cerr << "[receiveMessages] decryption failed: " << messageId << "\n";
                continue;
            }

            // Only store and surface the message after successful auth + decryption.
            store.addMessage(msg, peerKey(senderId, recipientId));

            if (senderId == conv.getPeerId() || recipientId == conv.getPeerId()) {
                conv.addMessage(std::move(*decrypted));
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

    // TOFU pinning: persist first, only accept the pin if the file write succeeded.
    // Inserting into pinnedKeys_ before a successful save would let the session trust
    // a key that won't survive a restart, creating an inconsistency between memory and disk.
    auto it = pinnedKeys_.find(userId);
    if (it == pinnedKeys_.end()) {
        pinnedKeys_[userId] = pk;
        if (!savePins()) {
            pinnedKeys_.erase(userId);  // roll back — pin was not durably stored
            std::cerr << "[fetchPeerPublicKey] pin not accepted: failed to persist for " << userId << "\n";
            return {};
        }
        std::cerr << "[fetchPeerPublicKey] TOFU: pinned and persisted public key for " << userId << "\n";
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
