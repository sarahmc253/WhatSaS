#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "Conversation.hpp"
#include "HttpClient.hpp"
#include "MessageStore.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// High-level messaging client with DHKEM(X25519) key establishment.
//
// Each message derives a fresh AES-256-GCM key via HPKE-inspired construction:
//   DH1 = X25519(eph_sk, recipient_pk)  — per-message freshness
//   DH2 = X25519(sender_sk, recipient_pk) — sender authentication
//   AES_KEY = HKDF-SHA256(DH1 || DH2)
//
// The ephemeral public key travels in the kem_output wire field.
// The shared secret never leaves the client. A server with full DB access
// cannot read plaintexts without the recipient's private key.
//
// Trust model: TOFU with local pinning. Peer public keys are fetched from
// the server key registry once and pinned in pinnedKeys_. Subsequent fetches
// that differ are rejected as potential key-substitution attacks.
class Client {
public:
    // staticSk / staticPk — local user's long-term X25519 keypair (32 bytes each).
    // pinsPath — path to the TOFU pin file (e.g. "~/.whatsas/pins.txt"). Created on
    //   first use. Existing pins are loaded at construction and survive restarts.
    // Throws std::invalid_argument if either key is not exactly 32 bytes.
    // Throws std::runtime_error if AES-NI hardware acceleration is unavailable.
    Client(const std::string& baseUrl,
           const std::string& senderId,
           std::vector<uint8_t> staticSk,
           std::vector<uint8_t> staticPk,
           const std::string& pinsPath,
           bool verifyCert = true);

    // Derive a per-message AES key via DHKEM, encrypt with AES-256-GCM,
    // and POST to baseUrl/messages. recipientPk must be the peer's registered
    // X25519 public key (32 bytes); obtain via fetchPeerPublicKey first.
    HttpResponse sendMessage(const std::string& recipientId,
                             const std::vector<uint8_t>& recipientPk,
                             const std::string& plaintext) const;

    // GET baseUrl/messages
    HttpResponse getMessages() const;

    // Fetch messages from server. For each valid message, extract kem_output,
    // re-derive the AES key via hpkeReceive(staticSk_, kem_output, senderPk),
    // decrypt, and store in store and conv.
    // senderPk — the peer's registered X25519 public key (TOFU-pinned).
    // Returns count of successfully decrypted messages, or -1 on HTTP/parse failure.
    int receiveMessages(MessageStore& store,
                        Conversation& conv,
                        const std::vector<uint8_t>& senderPk) const;

    // GET baseUrl/users/{userId}/key → base64-decode → 32-byte vector.
    // TOFU pinning: first fetch pins the key; later fetches that differ return empty
    // and log a key-substitution warning.
    // Returns empty vector on any network or parse error.
    std::vector<uint8_t> fetchPeerPublicKey(const std::string& userId) const;

    // POST baseUrl/keys  {"user_id": userId, "public_key": base64(pk)}
    // Publishes this client's public key to the server registry. Call once at startup.
    HttpResponse publishPublicKey(const std::string& userId,
                                  const std::vector<uint8_t>& publicKey) const;

private:
    std::string          baseUrl_;
    std::string          senderId_;
    std::vector<uint8_t> staticSk_;  // local long-term X25519 private key
    std::vector<uint8_t> staticPk_;  // local long-term X25519 public key
    bool                 verifyCert_;
    HttpClient           http_;
    std::string          pinsPath_;  // path to the on-disk TOFU pin file
    // TOFU key pins: userId → first-seen public key. mutable so const methods can update.
    mutable std::unordered_map<std::string, std::vector<uint8_t>> pinnedKeys_;

    // Load pins from pinsPath_ into pinnedKeys_. Called once at construction.
    void loadPins();
    // Append a single new pin to pinsPath_ (called only when a new pin is added).
    void savePins() const;
};

#endif // CLIENT_HPP
