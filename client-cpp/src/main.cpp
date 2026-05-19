#include "User.hpp"
#include "Message.hpp"
#include "MessageStore.hpp"
#include <iostream>
#include <ctime>

int main() {
    User alice("user-001", "alice", {0x01, 0x02, 0x03});
    User bob("user-002", "bob");

    Message m("msg-001", alice.getUserId(), bob.getUserId(),
              {0xAA, 0xBB}, {0x00, 0x01, 0x02}, std::time(nullptr));

    MessageStore store;
    store.addMessage(m);

    std::cout << "Messages for bob: " << store.getMessagesFor(bob.getUserId()).size() << "\n";
    std::cout << "Sorted total: "     << store.getSortedMessages().size() << "\n";

    const Message* found = store.findMessage("msg-001");
    std::cout << "Found msg-001: " << (found ? "yes" : "no") << "\n";
    std::cout << "Found bad-id: "  << (store.findMessage("bad-id") ? "yes" : "no") << "\n";

    return 0;
}
