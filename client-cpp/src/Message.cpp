#include "Message.hpp"

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
{}

const std::string& Message::getMessageId()   const { return messageId_;   }
const std::string& Message::getSenderId()    const { return senderId_;    }
const std::string& Message::getRecipientId() const { return recipientId_; }

const std::vector<uint8_t>& Message::getCiphertext() const { return ciphertext_; }
const std::vector<uint8_t>& Message::getNonce()      const { return nonce_;      }

std::time_t Message::getTimestamp() const { return timestamp_; }
