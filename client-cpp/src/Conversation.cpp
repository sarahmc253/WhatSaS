#include "../include/Conversation.hpp"
#include <algorithm>
#include <ctime>
#include <iostream>

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

void printConversation(const Conversation& conv) {
    auto messages = conv.getMessages();
    if (messages.empty()) {
        std::cout << "(no messages)\n";
        return;
    }
    for (const auto& dm : messages) {
        char buf[20];
        buf[0] = '\0';
        const std::time_t ts = dm.timestamp;
        if (std::tm* t = std::localtime(&ts)) {
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        }
        std::cout << "[" << buf << "] " << dm.senderId << ": " << dm.plaintext << "\n";
    }
}