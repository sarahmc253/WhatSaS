#ifndef CONVERSATION_HPP
#define CONVERSATION_HPP

#include "DecryptedMessage.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_set>
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

    // Peer's static X25519 public key — stored after first TOFU fetch from key registry.
    // Used by Client::receiveMessages to call hpkeReceive without a per-message lookup.
    void setPeerPublicKey(const std::vector<uint8_t>& pk);
    const std::vector<uint8_t>& getPeerPublicKey() const;
    bool hasPeerPublicKey() const;

    // Count messages in this conversation sent by senderId.
    std::size_t countMessagesFromSender(const std::string& senderId) const;

    // Count messages from a specific sender using std::count_if with a lambda.
    // Cleaner than a manual loop: no mutable counter, no risk of off-by-one,
    // and the intent is expressed directly in the predicate.
    std::size_t messageCount(const std::string& senderId) const;

    // Copy all messages sent by senderId, in insertion order.
    std::vector<DecryptedMessage> getMessagesFromSender(const std::string& senderId) const;

private:
    std::string peerId_;
    std::vector<DecryptedMessage> messages_;
    // unordered_set gives O(1) average lookup vs O(log n) for std::set — better
    // for the frequent duplicate checks on every incoming message.
    std::unordered_set<std::string> seenIds_;
    std::vector<uint8_t> peerPublicKey_;  // 32 bytes; empty until setPeerPublicKey called
};

// Print all decrypted messages in conv to stdout as:
//   [YYYY-MM-DD HH:MM:SS] senderId: plaintext
void printConversation(const Conversation& conv);

#endif // CONVERSATION_HPP
