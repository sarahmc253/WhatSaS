#include "../include/Conversation.hpp"
#include <algorithm>
#include <ctime>
#include <iostream>

Conversation::Conversation(std::string peerId)
    : peerId_(std::move(peerId)) {}

void Conversation::addMessage(DecryptedMessage dm) {
    if (seenIds_.find(dm.messageId) != seenIds_.end()) return;
    seenIds_.insert(dm.messageId);
    if (seenIds_.find(dm.messageId) != seenIds_.end()) return;
    seenIds_.insert(dm.messageId);
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

std::size_t Conversation::countMessagesFromSender(const std::string& senderId) const {
    return static_cast<std::size_t>(
        std::count_if(messages_.begin(), messages_.end(),
            [&senderId](const DecryptedMessage& dm) {
                return dm.senderId == senderId;
            }));
}

std::vector<DecryptedMessage> Conversation::getMessagesFromSender(const std::string& senderId) const {
    std::vector<DecryptedMessage> result;
    std::copy_if(messages_.begin(), messages_.end(),
        std::back_inserter(result),
        [&senderId](const DecryptedMessage& dm) {
            return dm.senderId == senderId;
        });
    return result;
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