#include "test_helpers.hpp"
#include "User.hpp"

void testUser(int& failed) {
    // Keyless constructor — key starts absent, set later via key exchange
    User u("u-1", "alice");
    CHECK_TRUE(!u.hasPublicKey());

    std::vector<uint8_t> key(32, 0xAB);
    u.setPublicKey(key);
    CHECK_TRUE(u.hasPublicKey());

    // Full constructor — key known upfront
    User u2("u-2", "bob", key);
    CHECK_TRUE(u2.hasPublicKey());
}
