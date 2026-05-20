#include "test_helpers.hpp"
#include "Message.hpp"
#include <ctime>

void testMessage(int& failed) {
    std::vector<uint8_t> ct  = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> non = {0x01, 0x02, 0x03};
    std::time_t ts = 1000000;

    Message m("msg-1", "sender-1", "recv-1", ct, non, ts);

    // Check routing fields — these are used by MessageStore and the server
    CHECK_EQ(m.getSenderId(),    std::string("sender-1"));
    CHECK_EQ(m.getRecipientId(), std::string("recv-1"));
    CHECK_EQ(m.getTimestamp(),   ts);
}
