#ifndef DECRYPTED_MESSAGE_HPP
#define DECRYPTED_MESSAGE_HPP

#include <ctime>
#include <string>

// A message after successful AES-256-GCM decryption — ready for display or storage.
// Defined in include/ so that Conversation.hpp (also in include/) can use it
// without depending on a src/ path.
struct DecryptedMessage {
    std::string messageId;
    std::string senderId;
    std::string recipientId;
    std::string plaintext;
    std::time_t timestamp;
};

#endif // DECRYPTED_MESSAGE_HPP
