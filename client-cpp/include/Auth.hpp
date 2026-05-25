#pragma once

#include <string>
#include "HttpClient.hpp"

class Auth {
public:
    static Auth registerUser(HttpClient& client,
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password);

    static Auth login(HttpClient& client,
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password);

    const std::string& getToken() const;
    bool isLoggedIn() const;

private:
    std::string token_;
};
