#define NOMINMAX

#include "../include/Client.hpp"
#include "hpke_utils.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
#include <cpuid.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <ctime>
#include <cstdio>
#ifdef _WIN32
#  include <windows.h>
#endif

Client::Client(const std::string& baseUrl,
               const std::string& senderId,
               std::vector<uint8_t> staticSk,
               std::vector<uint8_t> staticPk,
               const std::string& pinsPath,
               const std::string& authToken,
               bool verifyCert)
    : baseUrl_(baseUrl),
      senderId_(senderId),
      staticSk_(std::move(staticSk)),
      staticPk_(std::move(staticPk)),
      verifyCert_(verifyCert),
      http_(),
      pinsPath_(pinsPath),
      authToken_(authToken) {
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
    // Verify sk and pk are a matched X25519 keypair: derive pk from sk and compare.
    {
        unsigned char derived[crypto_scalarmult_BYTES];
        if (crypto_scalarmult_base(derived, staticSk_.data()) != 0 ||
            sodium_memcmp(derived, staticPk_.data(), 32) != 0) {
            throw std::invalid_argument(
                "staticSk and staticPk are not a matched X25519 keypair");
        }
    }
    // libsodium's AES-NI detection is broken in MSYS2/MinGW builds — use cpuid directly
    {
        unsigned int eax, ebx, ecx, edx;
        __get_cpuid(1, &eax, &ebx, &ecx, &edx);
        if (!((ecx >> 25) & 1))
            throw std::runtime_error("AES-256-GCM unavailable: hardware AES-NI required");
    }
    loadPins();
}

// File format: one line per pin — "<username> <base64(pk)> <uuid>\n"
// Lines starting with '#' are ignored. Malformed lines are skipped with a warning.
// The uuid column was added later; lines with only two tokens are loaded without UUID
// (remap check will only activate once the UUID is seen from the server again).
void Client::loadPins() {
    std::ifstream f(pinsPath_);
    if (!f.is_open()) return;  // file doesn't exist yet — that's fine on first run
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string userId, pkB64, uuid;
        if (!(ss >> userId >> pkB64)) {
            std::cerr << "[loadPins] skipping malformed line in " << pinsPath_ << "\n";
            continue;
        }
        ss >> uuid;  // optional — old pin files lack this column
        std::vector<uint8_t> pk = b64Decode(pkB64);
        if (pk.size() != 32) {
            std::cerr << "[loadPins] skipping bad key for " << userId << "\n";
            continue;
        }
        pinnedKeys_[userId] = std::move(pk);
        if (!uuid.empty()) pinnedIds_[userId] = uuid;
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
            f << userId << " " << b64Encode(pk.data(), pk.size());
            auto idIt = pinnedIds_.find(userId);
            if (idIt != pinnedIds_.end()) f << " " << idIt->second;
            f << "\n";
        }
        // flush + close before rename so all bytes are on disk
        f.flush();
        if (!f.good()) {
            std::cerr << "[savePins] write error for " << tmp << "\n";
            std::remove(tmp.c_str());
            return false;
        }
    }
#ifdef _WIN32
    // std::rename on the Windows CRT fails with EEXIST when the destination exists.
    // MoveFileExW with MOVEFILE_REPLACE_EXISTING is the correct atomic-replace primitive.
    auto toWide = [](const std::string& utf8) -> std::wstring {
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), len);
        return w;
    };
    const std::wstring wdest = toWide(pinsPath_);
    const std::wstring wtmp  = toWide(tmp);
    if (!MoveFileExW(wtmp.c_str(), wdest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "[savePins] MoveFileExW failed (" << GetLastError() << ")\n";
        DeleteFileW(wtmp.c_str());
        return false;
    }
#else
    if (std::rename(tmp.c_str(), pinsPath_.c_str()) != 0) {
        std::cerr << "[savePins] rename failed\n";
        std::remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

HttpResponse Client::sendMessage(const std::string& recipientUsername,
                                 const std::vector<uint8_t>& recipientPk,
                                 const std::string& plaintext) const {
    // Resolve username → UUID (must have been fetched via fetchPeerPublicKey first).
    auto idIt = pinnedIds_.find(recipientUsername);
    if (idIt == pinnedIds_.end()) {
        return {0, "", "recipient UUID unknown — call fetchPeerPublicKey first", false};
    }
    const std::string& recipientUuid = idIt->second;

    // 1. DHKEM: derive per-message AES key + ephemeral pk.
    auto hpkeResult = hpkeSend(staticSk_, recipientPk);
    if (!hpkeResult) {
        return {0, "", "HPKE key derivation failed (low-order point or bad key)", false};
    }

    // 2. Encrypt with derived AES-256-GCM key.
    // Use UUIDs for both sender and recipient in the AD so the receiver can reconstruct
    // the same AD from the server-returned sender_id and recipient_id fields.
    auto enc = encryptMessage(hpkeResult->aesKey, senderId_, recipientUuid, plaintext);
    sodium_memzero(hpkeResult->aesKey.data(), hpkeResult->aesKey.size());
    if (!enc) {
        return {0, "", "AES-256-GCM encryption failed", false};
    }

    // 3. Build wire body matching server schema.
    // Send message_id so the server stores it; both sides then use the same value in AD.
    // Encode ciphertext and nonce as hex to match the web client's wire format.
    auto ctBytes    = b64Decode(enc->ctB64);
    auto nonceBytes = b64Decode(enc->nonceB64);

    std::string ctHex(ctBytes.size() * 2 + 1, '\0');
    sodium_bin2hex(ctHex.data(), ctHex.size(), ctBytes.data(), ctBytes.size());
    ctHex.resize(ctBytes.size() * 2);

    char nonceHex[25];
    sodium_bin2hex(nonceHex, sizeof(nonceHex), nonceBytes.data(), nonceBytes.size());

    char ephHex[65];
    sodium_bin2hex(ephHex, sizeof(ephHex), hpkeResult->ephPk.data(), hpkeResult->ephPk.size());

    nlohmann::json body;
    body["recipient_id"] = recipientUuid;
    body["message_id"]   = enc->messageId;
    body["ciphertext"]   = ctHex;
    body["nonce"]        = std::string(nonceHex);
    body["ephemeral_pk"] = std::string(ephHex);

    return http_.post(baseUrl_ + "/messages", body.dump(), "application/json", authToken_, verifyCert_);
}

HttpResponse Client::getMessages() const {
    return http_.get(baseUrl_ + "/messages", authToken_, verifyCert_);
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

    // 3. Iterate message objects — server returns: id, sender_id, ciphertext, nonce,
    //    ephemeral_pk, created_at. We are always the recipient (server filters by JWT).
    int successCount = 0;
    for (const auto& obj : parsed["messages"]) {
        if (!obj.contains("id")          || !obj["id"].is_string()          ||
            !obj.contains("sender_id")   || !obj["sender_id"].is_string()   ||
            !obj.contains("nonce")       || !obj["nonce"].is_string()       ||
            !obj.contains("ciphertext")  || !obj["ciphertext"].is_string()  ||
            !obj.contains("created_at")  || !obj["created_at"].is_string()) {
            std::cerr << "[receiveMessages] skipping: missing or wrong-type field\n";
            continue;
        }

        std::string messageId  = obj["id"];
        std::string senderUuid = obj["sender_id"];
        std::string nonceB64   = obj["nonce"];
        std::string ctB64      = obj["ciphertext"];

        // Parse created_at → time_t.
        // Server may return ISO-8601 ("2026-05-25 16:35:57") or RFC 2822 ("Sat, 30 May 2026 14:13:02 GMT").
        static const char* RFC2822_MONTHS[] = {
            "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
        };
        std::tm tm{};
        std::string createdAt = obj["created_at"].get<std::string>();
        std::time_t ts = -1;

        // Try ISO-8601 first: "YYYY-MM-DD HH:MM:SS" (T separator also accepted)
        if (createdAt.size() > 10 && createdAt[10] == 'T') createdAt[10] = ' ';
        {
            int y, mo, d, h, mi, s;
            if (std::sscanf(createdAt.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
                tm = {}; tm.tm_year = y-1900; tm.tm_mon = mo-1; tm.tm_mday = d;
                tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
                ts = _mkgmtime(&tm);
            }
        }
        // Try RFC 2822: "Www, DD Mon YYYY HH:MM:SS GMT"
        if (ts < 0) {
            char monStr[4] = {};
            int d, y, h, mi, s;
            if (std::sscanf(createdAt.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, monStr, &y, &h, &mi, &s) == 6) {
                int mo = -1;
                for (int i = 0; i < 12; ++i)
                    if (std::strcmp(monStr, RFC2822_MONTHS[i]) == 0) { mo = i; break; }
                if (mo >= 0) {
                    tm = {}; tm.tm_year = y-1900; tm.tm_mon = mo; tm.tm_mday = d;
                    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
                    ts = _mkgmtime(&tm);
                }
            }
        }
        if (ts < 0) {
            std::cerr << "[receiveMessages] skipping: unparseable created_at '" << createdAt
                      << "' for message " << messageId << "\n";
            continue;
        }

        // 4. Validate ephemeral_pk field.
        if (!obj.contains("ephemeral_pk") || !obj["ephemeral_pk"].is_string()) {
            std::cerr << "[receiveMessages] skipping: missing ephemeral_pk: " << messageId << "\n";
            continue;
        }
        // ephemeral_pk is hex (web client) or base64 (old C++ client) — try hex first
        std::string ephPkStr = obj["ephemeral_pk"].get<std::string>();
        std::vector<uint8_t> ephPk;
        if (ephPkStr.size() == 64) {
            // 64 hex chars = 32 bytes
            ephPk.resize(32);
            if (sodium_hex2bin(ephPk.data(), 32, ephPkStr.c_str(), ephPkStr.size(),
                               nullptr, nullptr, nullptr) != 0)
                ephPk.clear();
        } else {
            ephPk = b64Decode(ephPkStr);
        }
        if (ephPk.size() != 32) {
            std::cerr << "[receiveMessages] skipping: ephemeral_pk must be 32 bytes: " << messageId << "\n";
            continue;
        }

        // 5. Re-derive the per-message AES key via DHKEM.
        std::vector<uint8_t> aesKey = hpkeReceive(staticSk_, ephPk, senderPk);
        if (aesKey.empty()) {
            std::cerr << "[receiveMessages] HPKE receive failed: " << messageId << "\n";
            continue;
        }

        // 6. Decrypt. nonce and ciphertext are hex (new) or base64 (old stored messages).
        // Detect hex: even length, all lowercase hex chars.
        auto hexDecode = [](const std::string& s) -> std::vector<uint8_t> {
            if (s.size() % 2 == 0 && s.find_first_not_of("0123456789abcdef") == std::string::npos) {
                std::vector<uint8_t> out(s.size() / 2);
                if (sodium_hex2bin(out.data(), out.size(), s.c_str(), s.size(),
                                   nullptr, nullptr, nullptr) == 0) return out;
            }
            return b64Decode(s);
        };
        std::vector<uint8_t> nonce = hexDecode(nonceB64);
        std::vector<uint8_t> ct    = hexDecode(ctB64);

        try {
            // Use senderUuid as senderId in the Message — conv filters by peerId (username),
            // so we surface the decrypted plaintext with the peer's username from conv.getPeerId().
            Message msg(messageId, senderUuid, senderId_, ct, nonce, ts);

            auto decrypted = decryptMessage(aesKey, msg);
            sodium_memzero(aesKey.data(), aesKey.size());
            if (!decrypted) {
                std::cerr << "[receiveMessages] decryption failed: " << messageId << "\n";
                continue;
            }

            store.addMessage(msg, peerKey(senderUuid, senderId_));

            // Override senderId in DecryptedMessage to the peer's username for display.
            decrypted->senderId = conv.getPeerId();
            conv.addMessage(std::move(*decrypted));
            ++successCount;
        } catch (const std::invalid_argument& e) {
            sodium_memzero(aesKey.data(), aesKey.size());
            std::cerr << "[receiveMessages] invalid message fields: " << e.what() << "\n";
        }
    }
    return successCount;
}

std::vector<uint8_t> Client::fetchPeerPublicKey(const std::string& userId) const {
    if (userId.empty()) {
        std::cerr << "[fetchPeerPublicKey] userId must not be empty\n";
        return {};
    }
    for (unsigned char c : userId) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') {
            std::cerr << "[fetchPeerPublicKey] userId '" << userId
                      << "' contains invalid character — only alphanumeric, '_', '-', '.' allowed\n";
            return {};
        }
    }
    // Server route: GET /users/{username}  → {id, username, x25519_public_key}
    HttpResponse resp = http_.get(baseUrl_ + "/users/" + userId, authToken_, verifyCert_);
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

    if (!parsed.contains("x25519_public_key") || !parsed["x25519_public_key"].is_string()) {
        std::cerr << "[fetchPeerPublicKey] missing x25519_public_key field\n";
        return {};
    }
    if (!parsed.contains("id") || !parsed["id"].is_string()) {
        std::cerr << "[fetchPeerPublicKey] missing id field\n";
        return {};
    }

    std::vector<uint8_t> pk = b64Decode(parsed["x25519_public_key"].get<std::string>());
    if (pk.size() != 32) {
        std::cerr << "[fetchPeerPublicKey] x25519_public_key must be 32 bytes, got "
                  << pk.size() << "\n";
        return {};
    }

    const std::string peerUuid = parsed["id"].get<std::string>();

    // TOFU pinning: persist first, only accept the pin if the file write succeeded.
    auto it = pinnedKeys_.find(userId);
    if (it == pinnedKeys_.end()) {
        pinnedKeys_[userId] = pk;
        pinnedIds_[userId]  = peerUuid;
        if (!savePins()) {
            pinnedKeys_.erase(userId);
            pinnedIds_.erase(userId);
            std::cerr << "[fetchPeerPublicKey] pin not accepted: failed to persist for " << userId << "\n";
            return {};
        }
        std::cerr << "[fetchPeerPublicKey] TOFU: pinned and persisted public key for " << userId << "\n";
    } else if (it->second != pk) {
        std::cerr << "[fetchPeerPublicKey] WARNING: public key for " << userId
                  << " differs from pinned key — possible key substitution attack. Rejecting.\n";
        return {};
    } else {
        // Key matches pin. Verify the UUID hasn't changed — a remap could redirect messages.
        auto idIt = pinnedIds_.find(userId);
        if (idIt != pinnedIds_.end() && idIt->second != peerUuid) {
            std::cerr << "[fetchPeerPublicKey] WARNING: UUID for " << userId
                      << " changed from " << idIt->second << " to " << peerUuid
                      << " — possible account takeover. Rejecting.\n";
            return {};
        }
        pinnedIds_[userId] = peerUuid;
    }

    return pk;
}

const std::vector<uint8_t>& Client::getPublicKey() const { return staticPk_; }

HttpResponse Client::publishPublicKey(const std::string& userId,
                                      const std::vector<uint8_t>& publicKey) const {
    nlohmann::json body;
    body["user_id"]    = userId;
    body["public_key"] = b64Encode(publicKey.data(), publicKey.size());
    return http_.post(baseUrl_ + "/keys", body.dump(), "application/json", authToken_, verifyCert_);
}
