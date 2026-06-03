#pragma once

#include <string>
#include "HttpClient.hpp"

class Auth {
public:
    static Auth registerUser(HttpClient& client,
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password,
        const std::string& x25519PublicKey,
        const std::string& wrappedPrivateKey,
        const std::string& kekSalt);

    static Auth login(HttpClient& client,
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password);

    bool logout(HttpClient& client, const std::string& baseUrl);

    void changePassword(HttpClient& client,
                        const std::string& baseUrl,
                        const std::string& oldPassword,
                        const std::string& newPassword,
                        const std::string& newWrappedPrivateKey,
                        const std::string& newKekSalt);

    const std::string& getToken()              const;
    const std::string& getUserId()             const;
    const std::string& getWrappedPrivateKey()  const;
    const std::string& getKekSalt()            const;
    bool isLoggedIn() const;

private:
    std::string token_;
    std::string userId_;             // UUID from login response — used for server calls
    std::string wrappedPrivateKey_;  // base64(nonce || ciphertext+tag), populated by login
    std::string kekSalt_;            // base64(crypto_pwhash_SALTBYTES), populated by login
};
