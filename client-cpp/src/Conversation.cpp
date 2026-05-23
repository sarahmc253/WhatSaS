#include "../include/Conversation.hpp"
#include <algorithm>

Conversation::Conversation(std::string peerId)
    : peerId_(std::move(peerId)) {}

void Conversation::addMessage(DecryptedMessage dm) {
    for (const auto& m : messages_)
        if (m.messageId == dm.messageId) return;
    messages_.push_back(std::move(dm));
}

std::vector<DecryptedMessage> Conversation::getMessages() const {
    std::vector<DecryptedMessage> sorted = messages_;
    std::sort(sorted.begin(), sorted.end(),
        [](const DecryptedMessage& a, const DecryptedMessage& b) {
            return a.timestamp < b.timestamp;
        });
    return sorted;
}

const std::string& Conversation::getPeerId() const {
    return peerId_;
}