#ifndef USER_HPP
#define USER_HPP

#include <string>
#include <vector>
#include <cstdint>

class User {
public:
    // Full constructor for use when public key is already known
    User(const std::string& userId,
         const std::string& username,
         const std::vector<uint8_t>& publicKey);

    // Keyless constructor for public key can be set later via setPublicKey
    User(const std::string& userId, const std::string& username);

    const std::string&          getUserId()    const;
    const std::string&          getUsername()  const;
    const std::vector<uint8_t>& getPublicKey() const;
    bool                        hasPublicKey() const;

    // Key may arrive after construction
    void setPublicKey(const std::vector<uint8_t>& key);

private:
    std::string          userId_;
    std::string          username_;
    std::vector<uint8_t> publicKey_;  // 32 bytes (Curve25519); empty until set
};

#endif // USER_HPP
