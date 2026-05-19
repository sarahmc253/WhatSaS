#ifndef MESSAGESTORE_HPP
#define MESSAGESTORE_HPP

#include "Message.hpp"
#include <vector>
#include <map>
#include <string>

class MessageStore {
public:
    MessageStore() = default;

    void addMessage(const Message& msg);

    // Returns nullptr if messageId not found
    const Message* findMessage(const std::string& messageId) const;

    // Returns messages for a recipient (derived index) and empty vector if none
    const std::vector<Message>& getMessagesFor(const std::string& recipientId) const;

    // Returns a copy of all messages sorted by timestamp ascending
    std::vector<Message> getSortedMessages() const;

    std::size_t size() const;

private:
    std::vector<Message>                        messages_;    // primary store, insertion order
    std::map<std::string, std::vector<Message>> byRecipient_; // derived index for recipient filtering

    // Returned by getMessagesFor when recipient has no messages
    static const std::vector<Message> empty_;
};

#endif // MESSAGESTORE_HPP
