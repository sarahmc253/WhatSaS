#include "Message.hpp"
#include <stdexcept>

Message::Message(const std::string& messageId,
                 const std::string& senderId,
                 const std::string& recipientId,
                 const std::vector<uint8_t>& ciphertext,
                 const std::vector<uint8_t>& nonce,
                 std::time_t timestamp)
    : messageId_(messageId)
    , senderId_(senderId)
    , recipientId_(recipientId)
    , ciphertext_(ciphertext)
    , nonce_(nonce)
    , timestamp_(timestamp)
{
    // Validate nonce: AES-256-GCM requires 12-byte nonce
    if (nonce_.size() != 12) {
        throw std::invalid_argument("nonce must be exactly 12 bytes for AES-256-GCM");
    }
    // Validate ciphertext: must be non-empty and at least 16 bytes (auth tag)
    if (ciphertext_.size() < 16) {
        throw std::invalid_argument("ciphertext must be at least 16 bytes (includes 16-byte auth tag)");
    }
}

const std::string& Message::getMessageId()   const { return messageId_;   }
const std::string& Message::getSenderId()    const { return senderId_;    }
const std::string& Message::getRecipientId() const { return recipientId_; }

const std::vector<uint8_t>& Message::getCiphertext() const { return ciphertext_; }
const std::vector<uint8_t>& Message::getNonce()      const { return nonce_;      }

std::time_t Message::getTimestamp() const { return timestamp_; }
