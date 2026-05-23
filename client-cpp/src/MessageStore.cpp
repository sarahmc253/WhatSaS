#include "MessageStore.hpp"
#include <algorithm>
#include <map>

const std::vector<Message> MessageStore::empty_{};

void MessageStore::addMessage(const Message& msg, const std::string& key) {
    messages_.push_back(msg);
    byPeer_[key].push_back(msg);
}

std::optional<Message> MessageStore::findMessage(const std::string& messageId) const {
    auto it = std::find_if(messages_.begin(), messages_.end(),
        [&messageId](const Message& m) {
            return m.getMessageId() == messageId;
        });
    return it != messages_.end() ? std::optional<Message>(*it) : std::nullopt;
}

const std::vector<Message>& MessageStore::getMessagesFor(const std::string& key) const {
    auto it = byPeer_.find(key);
    return it != byPeer_.end() ? it->second : empty_;
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

std::map<std::string, std::size_t> MessageStore::getSenderFrequencies() const {
    std::map<std::string, std::size_t> freq;
    for (const Message& m : messages_) {
        freq[m.getSenderId()]++;
    }
    return freq;
}