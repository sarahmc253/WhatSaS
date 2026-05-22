#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "HttpClient.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// High-level messaging client: manages AES-256-GCM encryption and HTTP transport.
// The caller is responsible for deriving aesKey via HKDF (future task).
class Client {
public:
    // aesKey must be exactly 32 bytes (AES-256-GCM key size).
    // Throws std::invalid_argument if key is wrong size.
    // Throws std::runtime_error if AES-NI hardware acceleration is unavailable
    // (libsodium's crypto_aead_aes256gcm requires hardware AES support).
    Client(const std::string& baseUrl,
           const std::string& senderId,
           std::vector<uint8_t> aesKey,
           bool verifyCert = true);

    // Encrypt plaintext with AES-256-GCM (CSPRNG nonce, AD = canonical JSON),
    // then POST the encrypted message to baseUrl + "/messages".
    HttpResponse sendMessage(const std::string& recipientId,
                             const std::string& plaintext) const;

    // GET baseUrl + "/messages"
    HttpResponse getMessages() const;

private:
    std::string              baseUrl_;
    std::string              senderId_;
    std::vector<uint8_t>     aesKey_;        // 32 bytes; caller derives via HKDF
    bool                     verifyCert_;
    HttpClient               http_;
    // Counter-based nonce: base XOR'd with an incrementing value per message.
    // Avoids 96-bit birthday collision risk of purely random nonces when many
    // messages are sent under the same key.
    mutable std::atomic<uint64_t> nonceCounter_;
    unsigned char                 nonceBase_[12];  // AES-256-GCM nonce is always 12 bytes (96 bits)
};

#endif // CLIENT_HPP
