#include "User.hpp"
#include <stdexcept>

User::User(const std::string& userId,
           const std::string& username,
           const std::vector<uint8_t>& publicKey)
    : userId_(userId)
    , username_(username)
    , publicKey_(publicKey)
{
    if (publicKey_.size() != 32) {
        throw std::invalid_argument("publicKey must be exactly 32 bytes for Curve25519");
    }
}

User::User(const std::string& userId, const std::string& username)
    : userId_(userId)
    , username_(username)
{}

const std::string&          User::getUserId()    const { return userId_;    }
const std::string&          User::getUsername()  const { return username_;  }
const std::vector<uint8_t>& User::getPublicKey() const { return publicKey_; }

bool User::hasPublicKey() const { return !publicKey_.empty(); }

void User::setPublicKey(const std::vector<uint8_t>& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("publicKey must be exactly 32 bytes for Curve25519");
    }
    publicKey_ = key;
}
