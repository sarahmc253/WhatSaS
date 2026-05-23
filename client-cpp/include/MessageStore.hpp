#ifndef MESSAGESTORE_HPP
#define MESSAGESTORE_HPP

#include "Message.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

class MessageStore {
public:
    MessageStore() = default;

    // peerKey must be derived by the caller as min(senderId, recipientId) so that
    // both directions of the same conversation land in the same bucket.
    // MessageStore has no identity of its own and imposes no ordering convention.
    void addMessage(const Message& msg, const std::string& peerKey);

    // Returns a copy of the message if found, or empty optional if not found.
    std::optional<Message> findMessage(const std::string& messageId) const;

    // Returns all messages for a peer conversation (both directions).
    // Returns empty vector if no messages exist for peerKey.
    const std::vector<Message>& getMessagesFor(const std::string& peerKey) const;

    // Returns a copy of all messages sorted by timestamp ascending.
    std::vector<Message> getSortedMessages() const;

    std::size_t size() const;

private:
    std::vector<Message>                        messages_;  // primary store, insertion order
    std::map<std::string, std::vector<Message>> byPeer_;    // index: peerKey -> messages

    static const std::vector<Message> empty_;
};

// Derive the canonical peer key for a message: both directions of alice<->bob
// map to the same key. Components are percent-encoded so IDs containing '|'
// or '%' cannot collide with the delimiter.
inline std::string peerKey(const std::string& senderId, const std::string& recipientId) {
    auto escape = [](const std::string& id) {
        std::string out;
        out.reserve(id.size());
        for (unsigned char c : id) {
            if (c == '%') out += "%25";
            else if (c == '|') out += "%7C";
            else out += static_cast<char>(c);
        }
        return out;
    };
    std::string a = escape(senderId);
    std::string b = escape(recipientId);
    return (a < b) ? a + "|" + b : b + "|" + a;
}

#endif // MESSAGESTORE_HPP