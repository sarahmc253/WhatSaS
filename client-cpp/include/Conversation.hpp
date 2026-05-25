#ifndef CONVERSATION_HPP
#define CONVERSATION_HPP

#include "DecryptedMessage.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <vector>

// Organises already-decrypted messages for a single peer conversation.
// Has no knowledge of keys or cryptography — it receives DecryptedMessage
// objects from Client and stores/retrieves them.
class Conversation {
public:
    explicit Conversation(std::string peerId);

    // Store a decrypted message. Duplicate message IDs are silently ignored.
    void addMessage(DecryptedMessage dm);

    // All stored messages, sorted by timestamp ascending.
    std::vector<DecryptedMessage> getMessages() const;

    const std::string& getPeerId() const;

    // Count messages in this conversation sent by senderId.
    std::size_t countMessagesFromSender(const std::string& senderId) const;

    // Copy all messages sent by senderId, in insertion order.
    std::vector<DecryptedMessage> getMessagesFromSender(const std::string& senderId) const;

private:
    std::string peerId_;
    std::vector<DecryptedMessage> messages_;
    std::set<std::string> seenIds_;
};

// Print all decrypted messages in conv to stdout as:
//   [YYYY-MM-DD HH:MM:SS] senderId: plaintext
void printConversation(const Conversation& conv);

#endif // CONVERSATION_HPP
