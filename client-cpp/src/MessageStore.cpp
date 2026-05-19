#include "MessageStore.hpp"
#include <algorithm>

const std::vector<Message> MessageStore::empty_{};

void MessageStore::addMessage(const Message& msg) {
    messages_.push_back(msg);
    // operator[] creates the bucket if this recipient has not been seen before
    byRecipient_[msg.getRecipientId()].push_back(msg);
}

const Message* MessageStore::findMessage(const std::string& messageId) const {
    auto it = std::find_if(messages_.begin(), messages_.end(),
        [&messageId](const Message& m) {
            return m.getMessageId() == messageId;
        });
    return it != messages_.end() ? &(*it) : nullptr;
}

const std::vector<Message>& MessageStore::getMessagesFor(const std::string& recipientId) const {
    auto it = byRecipient_.find(recipientId);
    return it != byRecipient_.end() ? it->second : empty_;
}

std::vector<Message> MessageStore::getSortedMessages() const {
    std::vector<Message> sorted = messages_;
    std::sort(sorted.begin(), sorted.end(),
        [](const Message& a, const Message& b) {
            return a.getTimestamp() < b.getTimestamp();
        });
    return sorted;
}

std::size_t MessageStore::size() const { return messages_.size(); }
