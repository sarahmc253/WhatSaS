#ifndef MESSAGE_CRYPTO_HPP
#define MESSAGE_CRYPTO_HPP

#include "../include/DecryptedMessage.hpp"
#include "../include/Message.hpp"
#include "crypto_utils.hpp"
#include <ctime>
#include <optional>
#include <string>
#include <vector>

// Output of encryptMessage — the base64-encoded fields for the server JSON body.
struct EncryptedBlob {
    std::string messageId;  // 32-char hex (CSPRNG)
    std::string nonceB64;   // base64(12-byte nonce)
    std::string ctB64;      // base64(ciphertext + 16-byte AEAD tag)
    std::time_t timestamp;
};

// Encrypt plaintext with AES-256-GCM. Generates a fresh message ID and timestamp.
// Returns nullopt if encryption fails (wrong key size or AES-NI unavailable).
static inline std::optional<EncryptedBlob> encryptMessage(
    const std::vector<uint8_t>& aesKey,
    const std::string& senderId,
    const std::string& recipientId,
    const std::string& plaintext)
{
    const std::string msgId = generateMsgId();
    const std::time_t ts    = std::time(nullptr);
    const std::string ad    = buildAd(senderId, recipientId, msgId, ts);

    const std::vector<uint8_t> blob = encryptAes256Gcm(aesKey, plaintext, ad);
    if (blob.empty()) return std::nullopt;

    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;  // 12
    EncryptedBlob out;
    out.messageId = msgId;
    out.nonceB64  = b64Encode(blob.data(), nonceLen);
    out.ctB64     = b64Encode(blob.data() + nonceLen, blob.size() - nonceLen);
    out.timestamp = ts;
    return out;
}

// Decrypt a received Message using the provided AES-256-GCM key.
// Reconstructs the canonical AD from the Message fields — must match exactly
// what the sender built in encryptMessage above.
// Returns nullopt on authentication failure or wrong key size. Does not throw.
static inline std::optional<DecryptedMessage> decryptMessage(
    const std::vector<uint8_t>& aesKey,
    const Message& msg)
{
    const std::string ad = buildAd(
        msg.getSenderId(),
        msg.getRecipientId(),
        msg.getMessageId(),
        msg.getTimestamp());

    // decryptAes256Gcm expects nonce (12 bytes) || ciphertext+tag as one blob.
    // Message stores them separately; concatenate for the call.
    std::vector<uint8_t> noncePlusCt;
    noncePlusCt.reserve(msg.getNonce().size() + msg.getCiphertext().size());
    noncePlusCt.insert(noncePlusCt.end(),
                       msg.getNonce().begin(), msg.getNonce().end());
    noncePlusCt.insert(noncePlusCt.end(),
                       msg.getCiphertext().begin(), msg.getCiphertext().end());

    auto ptOpt = decryptAes256Gcm(aesKey, noncePlusCt, ad);
    if (!ptOpt) return std::nullopt;
    const std::vector<uint8_t>& ptBytes = *ptOpt;

    DecryptedMessage dm;
    dm.messageId   = msg.getMessageId();
    dm.senderId    = msg.getSenderId();
    dm.recipientId = msg.getRecipientId();
    dm.plaintext   = std::string(
        reinterpret_cast<const char*>(ptBytes.data()), ptBytes.size());
    dm.timestamp   = msg.getTimestamp();
    return dm;
}

#endif // MESSAGE_CRYPTO_HPP