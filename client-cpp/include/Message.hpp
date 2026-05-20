#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

class Message {
public:
    Message(const std::string& messageId,
            const std::string& senderId,
            const std::string& recipientId,
            const std::vector<uint8_t>& ciphertext,
            const std::vector<uint8_t>& nonce,
            std::time_t timestamp);

    const std::string&          getMessageId()   const;
    const std::string&          getSenderId()    const;
    const std::string&          getRecipientId() const;
    const std::vector<uint8_t>& getCiphertext()  const;
    const std::vector<uint8_t>& getNonce()       const;
    std::time_t                 getTimestamp()   const;

private:
    std::string          messageId_;
    std::string          senderId_;
    std::string          recipientId_;
    std::vector<uint8_t> ciphertext_;  // AES-256-GCM ciphertext + 16-byte auth tag
    std::vector<uint8_t> nonce_;       // 12 bytes (AES-256-GCM nonce size)
    std::time_t          timestamp_;
};

#endif // MESSAGE_HPP
