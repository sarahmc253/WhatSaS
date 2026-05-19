#include "User.hpp"

User::User(const std::string& userId,
           const std::string& username,
           const std::vector<uint8_t>& publicKey)
    : userId_(userId)
    , username_(username)
    , publicKey_(publicKey)
{}

User::User(const std::string& userId, const std::string& username)
    : userId_(userId)
    , username_(username)
{}

const std::string&          User::getUserId()    const { return userId_;    }
const std::string&          User::getUsername()  const { return username_;  }
const std::vector<uint8_t>& User::getPublicKey() const { return publicKey_; }

bool User::hasPublicKey() const { return !publicKey_.empty(); }

void User::setPublicKey(const std::vector<uint8_t>& key) { publicKey_ = key; }
