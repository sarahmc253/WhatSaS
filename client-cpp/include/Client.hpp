#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "Conversation.hpp"
#include "HttpClient.hpp"
#include "MessageStore.hpp"
#include <cstdint>
#include <string>
#include <vector>

// High-level messaging client: manages AES-256-GCM encryption and HTTP transport.
// aesKey is held temporarily; it will be replaced by an HKDF-derived key from
// the HPKE handshake once the Conversation class is implemented.
class Client {
public:
    // aesKey must be exactly 32 bytes (AES-256-GCM key size).
    // Throws std::invalid_argument if key is wrong size.
    // Throws std::runtime_error if AES-NI hardware acceleration is unavailable.
    Client(const std::string& baseUrl,
           const std::string& senderId,
           std::vector<uint8_t> aesKey,
           bool verifyCert = true);

    // Encrypt plaintext with AES-256-GCM (fresh CSPRNG nonce per call, AD = canonical JSON),
    // then POST the encrypted message to baseUrl + "/messages".
    HttpResponse sendMessage(const std::string& recipientId,
                             const std::string& plaintext) const;

    // GET baseUrl + "/messages"
    HttpResponse getMessages() const;

    // Fetch messages from server, parse JSON, decode base64.
    // Each valid raw Message is stored in store (keyed by canonical peer).
    // Each successfully decrypted message is stored in conv.
    // Returns count of successfully decrypted messages, or -1 on HTTP/parse failure.
    int receiveMessages(MessageStore& store, Conversation& conv) const;

private:
    std::string          baseUrl_;
    std::string          senderId_;
    // TEMPORARY: caller passes a hardcoded key; replace with HKDF-derived key
    // from HPKE handshake (Conversation class, future task).
    std::vector<uint8_t> aesKey_;
    bool                 verifyCert_;
    HttpClient           http_;
};

#endif // CLIENT_HPP