#define NOMINMAX

#include "../include/Client.hpp"
#include "hpke_utils.hpp"
#include "message_crypto.hpp"

#include <sodium.h>
#if defined(_MSC_VER)
#  include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#  include <cpuid.h>
#endif
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

// Portable UTC mktime: _mkgmtime on Windows, timegm on POSIX.
#ifdef _WIN32
static inline std::time_t portable_mkgmtime(std::tm* t) { return _mkgmtime(t); }
#else
static inline std::time_t portable_mkgmtime(std::tm* t) { return timegm(t); }
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
    // libsodium's AES-NI detection is broken in MSYS2/MinGW builds â€" use cpuid directly
    {
        bool aesni = false;
#if defined(_MSC_VER)
        int info[4] = {};
        __cpuid(info, 1);
        aesni = (info[2] >> 25) & 1;
#elif defined(__GNUC__) || defined(__clang__)
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
            aesni = (ecx >> 25) & 1;
#endif
        if (!aesni)
            throw std::runtime_error("AES-256-GCM unavailable: hardware AES-NI required");
    }
    loadPins();
}

// Hex-encode len bytes of buf into a std::string.
static std::string toHex(const unsigned char* buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[(buf[i] >> 4) & 0xf];
        out += hex[buf[i] & 0xf];
    }
    return out;
}

// Compute HMAC-SHA256 over `body` keyed with `key` (must be 32 bytes).
static std::array<unsigned char, crypto_auth_hmacsha256_BYTES>
computeHmac(const std::string& body, const std::vector<uint8_t>& key) {
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> mac{};
    crypto_auth_hmacsha256(mac.data(),
        reinterpret_cast<const unsigned char*>(body.data()), body.size(),
        key.data());
    return mac;
}

// File format: one line per pin â€" "<username> <base64(pk)> <uuid>\n"
// Lines starting with '#HMAC ' carry the HMAC-SHA256 trailer; other '#' lines are ignored.
// Malformed lines are skipped with a warning.
// The uuid column was added later; lines with only two tokens are loaded without UUID
// (remap check will only activate once the UUID is seen from the server again).
void Client::loadPins() {
    std::ifstream f(pinsPath_);
    if (!f.is_open()) return;  // file doesn't exist yet â€" that's fine on first run

    std::string storedHmac;
    std::string body;       // accumulates all non-HMAC lines for verification
    std::vector<std::pair<std::string,std::string>> rawLines; // (userId, rest) pre-parse

    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 6 && line.substr(0, 6) == "#HMAC ") {
            storedHmac = line.substr(6);
            continue;
        }
        body += line + "\n";
        if (line.empty() || line[0] == '#') continue;
        rawLines.push_back({line, ""});
    }

    if (!storedHmac.empty()) {
        auto mac = computeHmac(body, staticSk_);
        std::string expected = toHex(mac.data(), mac.size());
        if (storedHmac != expected) {
            return;
        }
    }

    for (const auto& [rawLine, _] : rawLines) {
        std::istringstream ss(rawLine);
        std::string userId, pkB64, uuid;
        if (!(ss >> userId >> pkB64)) continue;
        ss >> uuid;
        std::vector<uint8_t> pk = b64Decode(pkB64);
        if (pk.size() != 32) continue;
        pinnedKeys_[userId] = std::move(pk);
        if (!uuid.empty()) pinnedIds_[userId] = uuid;
    }
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
            return false;
        }
        // Build body first so we can HMAC it before writing.
        std::string body;
        body += "# WhatSaS TOFU key pins - do not edit manually\n";
        for (const auto& [userId, pk] : pinnedKeys_) {
            body += userId + " " + b64Encode(pk.data(), pk.size());
            auto idIt = pinnedIds_.find(userId);
            if (idIt != pinnedIds_.end()) body += " " + idIt->second;
            body += "\n";
        }
        auto mac = computeHmac(body, staticSk_);
        f << body << "#HMAC " << toHex(mac.data(), mac.size()) << "\n";
        // flush + close before rename so all bytes are on disk
        f.flush();
        if (!f.good()) {
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
        DeleteFileW(wtmp.c_str());
        return false;
    }
#else
    if (std::rename(tmp.c_str(), pinsPath_.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

HttpResponse Client::sendMessage(const std::string& recipientUsername,
                                 const std::vector<uint8_t>& recipientPk,
                                 const std::string& plaintext,
                                 std::string& sentMessageId) const {
    sentMessageId.clear();
    // Resolve username â†' UUID (must have been fetched via fetchPeerPublicKey first).
    auto idIt = pinnedIds_.find(recipientUsername);
    if (idIt == pinnedIds_.end()) {
        return {0, "", "recipient UUID unknown - call fetchPeerPublicKey first", false};
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

    const int64_t sendTs = static_cast<int64_t>(std::time(nullptr));

    nlohmann::json body;
    body["recipient_id"] = recipientUuid;
    body["message_id"]   = enc->messageId;
    body["ciphertext"]   = ctHex;
    body["nonce"]        = std::string(nonceHex);
    body["ephemeral_pk"] = std::string(ephHex);
    body["timestamp"]    = sendTs;

    if (recipientUuid.empty() || ctHex.empty() ||
        std::string(nonceHex).empty() || std::string(ephHex).empty()) {
        return {0, "", "sendMessage: one or more required fields are empty", false};
    }

    auto resp = http_.post(baseUrl_ + "/messages", body.dump(), "application/json", authToken_, verifyCert_);
    if (resp.ok_) sentMessageId = enc->messageId;
    return resp;
}

HttpResponse Client::getMessages() const {
    return http_.get(baseUrl_ + "/messages", authToken_, verifyCert_);
}

int Client::receiveMessages(MessageStore& store,
                            Conversation& conv,
                            const std::vector<uint8_t>& senderPk,
                            const std::unordered_map<std::string, std::string>& sentCache) const {
    // 1. HTTP fetch
    HttpResponse resp = getMessages();
    if (!resp.ok_ || resp.statusCode_ != 200 || resp.body_.empty()) {
        return -1;
    }

    // 2. Parse entire response body as JSON
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        return -1;
    }

    if (!parsed.contains("messages") || !parsed["messages"].is_array()) {
        return -1;
    }

    // 3. Iterate message objects. Server returns all messages (sent + received).
    //    Only process messages that belong to this conversation (with conv.getPeerId()),
    //    and skip sent messages since we can't decrypt our own ciphertext.
    const std::string& peerId = conv.getPeerId();
    int successCount = 0;
    for (const auto& obj : parsed["messages"]) {
        // Filter to this conversation only.
        const std::string senderUsername    = obj.contains("sender_username")    && obj["sender_username"].is_string()    ? obj["sender_username"].get<std::string>()    : "";
        const std::string recipientUsername = obj.contains("recipient_username") && obj["recipient_username"].is_string() ? obj["recipient_username"].get<std::string>() : "";
        const bool fromPeer = (senderUsername == peerId);
        const bool toPeer   = (recipientUsername == peerId);
        if (!fromPeer && !toPeer) continue;  // belongs to a different conversation

        if (obj.contains("direction") && obj["direction"].is_string() &&
            obj["direction"].get<std::string>() == "sent") {
            // Can't decrypt our own sent ciphertext, but show as placeholder for continuity.
            if (obj.contains("id") && obj["id"].is_string() &&
                obj.contains("timestamp") && obj["timestamp"].is_number_integer()) {
                DecryptedMessage dm;
                dm.messageId   = obj["id"].get<std::string>();
                dm.senderId    = senderUsername;
                dm.recipientId = recipientUsername;
                const bool revoked = obj.contains("is_revoked") && obj["is_revoked"].is_number_integer() && obj["is_revoked"].get<int>() != 0;
                if (revoked) {
                    dm.plaintext = "[revoked]";
                } else {
                    auto it = sentCache.find(dm.messageId);
                    dm.plaintext = (it != sentCache.end()) ? it->second : "[message sent]";
                }
                dm.timestamp   = static_cast<std::time_t>(obj["timestamp"].get<long long>());
                conv.addMessage(std::move(dm));
            }
            continue;
        }
        if (!obj.contains("id")          || !obj["id"].is_string()          ||
            !obj.contains("sender_id")   || !obj["sender_id"].is_string()   ||
            !obj.contains("nonce")       || !obj["nonce"].is_string()       ||
            !obj.contains("ciphertext")  || !obj["ciphertext"].is_string()) {
            continue;
        }

        std::string messageId  = obj["id"];
        std::string senderUuid = obj["sender_id"];
        std::string nonceB64   = obj["nonce"];
        std::string ctB64      = obj["ciphertext"];

        // Prefer the application-level 'timestamp' (integer unix epoch baked into the AD
        // at encrypt time). Fall back to parsing 'created_at' only when 'timestamp' is absent
        // â€" the two differ and using created_at breaks AES-GCM AD verification.
        std::time_t ts = -1;
        if (obj.contains("timestamp") && obj["timestamp"].is_number_integer()) {
            ts = static_cast<std::time_t>(obj["timestamp"].get<long long>());
        } else {
            static const char* RFC2822_MONTHS[] = {
                "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
            };
            std::tm tm{};
            std::string createdAt = obj["created_at"].get<std::string>();
            if (createdAt.size() > 10 && createdAt[10] == 'T') createdAt[10] = ' ';
            {
                int y, mo, d, h, mi, s;
                if (std::sscanf(createdAt.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
                    tm = {}; tm.tm_year = y-1900; tm.tm_mon = mo-1; tm.tm_mday = d;
                    tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = s; tm.tm_isdst = -1;
                    ts = portable_mkgmtime(&tm);
                }
            }
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
                        ts = portable_mkgmtime(&tm);
                    }
                }
            }
        }
        if (ts < 0) {
            continue;
        }

        // 4. Validate ephemeral_pk field.
        if (!obj.contains("ephemeral_pk") || !obj["ephemeral_pk"].is_string()) {
            continue;
        }
        // ephemeral_pk is hex (web client) or base64 (old C++ client) â€" try hex first
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
            continue;
        }

        // 5. Re-derive the per-message AES key via DHKEM.
        // Prefer sender_x25519_public_key from the message (always base64) over the
        // TOFU-pinned senderPk argument â€" it's already validated by the server and
        // avoids failures when multiple senders are in the same inbox fetch.
        std::vector<uint8_t> msgSenderPk = senderPk;
        if (obj.contains("sender_x25519_public_key") && obj["sender_x25519_public_key"].is_string()) {
            auto candidate = b64Decode(obj["sender_x25519_public_key"].get<std::string>());
            if (candidate.size() == 32) msgSenderPk = std::move(candidate);
        }
        std::vector<uint8_t> aesKey = hpkeReceive(staticSk_, ephPk, msgSenderPk);
        if (aesKey.empty()) {
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
            // Use senderUuid as senderId in the Message â€" conv filters by peerId (username),
            // so we surface the decrypted plaintext with the peer's username from conv.getPeerId().
            Message msg(messageId, senderUuid, senderId_, ct, nonce, ts);

            auto decrypted = decryptMessage(aesKey, msg);
            sodium_memzero(aesKey.data(), aesKey.size());
            if (!decrypted) {
                continue;
            }

            store.addMessage(msg, peerKey(senderUuid, senderId_));

            // Use the actual sender username for display (already filtered to this conv).
            decrypted->senderId    = senderUsername.empty() ? conv.getPeerId() : senderUsername;
            decrypted->recipientId = recipientUsername;
            // Overwrite plaintext if the message was revoked server-side.
            if (obj.contains("is_revoked") && obj["is_revoked"].is_number_integer() && obj["is_revoked"].get<int>() != 0)
                decrypted->plaintext = "[revoked]";
            conv.addMessage(std::move(*decrypted));
            ++successCount;
        } catch (const std::invalid_argument& e) {
            sodium_memzero(aesKey.data(), aesKey.size());
        }
    }
    return successCount;
}

std::vector<uint8_t> Client::fetchPeerPublicKey(const std::string& userId) const {
    if (userId.empty()) {
        return {};
    }
    for (unsigned char c : userId) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') {  // NOLINT
            return {};
        }
    }
    HttpResponse resp = http_.get(baseUrl_ + "/users/" + userId, authToken_, verifyCert_);
    if (!resp.ok_ || resp.statusCode_ != 200 || resp.body_.empty()) {
        return {};
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        return {};
    }

    if (!parsed.contains("x25519_public_key") || !parsed["x25519_public_key"].is_string()) {
        return {};
    }
    if (!parsed.contains("id") || !parsed["id"].is_string()) {
        return {};
    }

    std::vector<uint8_t> pk = b64Decode(parsed["x25519_public_key"].get<std::string>());
    if (pk.size() != 32) {
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
            return {};
        }
    } else if (it->second != pk) {
        return {};
    } else {
        auto idIt = pinnedIds_.find(userId);
        if (idIt != pinnedIds_.end() && idIt->second != peerUuid) {
            return {};
        }
        pinnedIds_[userId] = peerUuid;
    }

    return pk;
}

const std::vector<uint8_t>& Client::getPublicKey() const { return staticPk_; }

HttpResponse Client::deleteMessage(const std::string& messageId) const {
    return http_.del(baseUrl_ + "/messages/" + messageId, authToken_, verifyCert_);
}

HttpResponse Client::revokeMessage(const std::string& messageId) const {
    return http_.postNoBody(baseUrl_ + "/messages/" + messageId + "/revoke", authToken_, verifyCert_);
}

HttpResponse Client::getMessage(const std::string& messageId) const {
    return http_.get(baseUrl_ + "/messages/" + messageId, authToken_, verifyCert_);
}

HttpResponse Client::forwardMessage(const std::string& originalMessageId,
                                    const std::string& recipientUsername,
                                    const std::vector<uint8_t>& recipientPk,
                                    const std::string& plaintext) const {
    // Resolve username â†' UUID.
    auto idIt = pinnedIds_.find(recipientUsername);
    if (idIt == pinnedIds_.end())
        return {0, "", "recipient UUID unknown - call fetchPeerPublicKey first", false};
    const std::string& recipientUuid = idIt->second;

    // Re-encrypt the plaintext for the new recipient.
    auto hpkeResult = hpkeSend(staticSk_, recipientPk);
    if (!hpkeResult)
        return {0, "", "HPKE key derivation failed", false};

    auto enc = encryptMessage(hpkeResult->aesKey, senderId_, recipientUuid, plaintext);
    sodium_memzero(hpkeResult->aesKey.data(), hpkeResult->aesKey.size());
    if (!enc)
        return {0, "", "AES-256-GCM encryption failed", false};

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
    body["recipientUsername"]        = recipientUsername;
    body["message_id"]               = enc->messageId;
    body["ciphertext"]               = ctHex;
    body["nonce"]                    = std::string(nonceHex);
    body["ephemeral_pk"]             = std::string(ephHex);
    body["timestamp"]                = static_cast<int64_t>(enc->timestamp);
    body["sender_x25519_public_key"] = b64Encode(staticPk_.data(), staticPk_.size());

    return http_.post(baseUrl_ + "/messages/" + originalMessageId + "/forward",
                      body.dump(), "application/json", authToken_, verifyCert_);
}

HttpResponse Client::publishPublicKey(const std::string& userId,
                                      const std::vector<uint8_t>& publicKey) const {
    nlohmann::json body;
    body["user_id"]    = userId;
    body["public_key"] = b64Encode(publicKey.data(), publicKey.size());
    return http_.post(baseUrl_ + "/keys", body.dump(), "application/json", authToken_, verifyCert_);
}
