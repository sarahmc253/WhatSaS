#include "test_helpers.hpp"
#include "MessageStore.hpp"

void testStore(int& failed) {
    // t2 < t1 intentionally — verifies sort order
    std::time_t t1 = 1000, t2 = 500;
    Message m1("id-1", "alice", "bob",  {0x01}, {0xA1}, t1);
    Message m2("id-2", "alice", "bob",  {0x02}, {0xA2}, t2);
    Message m3("id-3", "carol", "dave", {0x03}, {0xA3}, t1);

    MessageStore store;
    store.addMessage(m1);
    store.addMessage(m2);
    store.addMessage(m3);

    // Recipient index returns correct counts
    CHECK_EQ(store.getMessagesFor("bob").size(),  std::size_t(2));
    CHECK_TRUE(store.getMessagesFor("nobody").empty());

    // findMessage returns pointer on hit, nullptr on miss
    CHECK_TRUE(store.findMessage("id-1") != nullptr);
    CHECK_TRUE(store.findMessage("bad-id") == nullptr);

    // getSortedMessages — t2=500 must come before t1=1000
    auto sorted = store.getSortedMessages();
    CHECK_TRUE(sorted[0].getTimestamp() <= sorted[1].getTimestamp());
}
