#include "../include/Auth.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>

// ── Accessors ────────────────────────────────────────────────────────────────

const std::string& Auth::getToken()             const { return token_;             }
const std::string& Auth::getUserId()            const { return userId_;            }
const std::string& Auth::getWrappedPrivateKey() const { return wrappedPrivateKey_; }
const std::string& Auth::getKekSalt()           const { return kekSalt_;           }

bool Auth::isLoggedIn() const {
    return !token_.empty();
}

// ── Instance methods ─────────────────────────────────────────────────────────

void Auth::logout(HttpClient& client, const std::string& baseUrl) {
    const auto resp = client.post(baseUrl + "/auth/logout", "", "application/json", token_);
    if (resp.statusCode_ >= 200 && resp.statusCode_ <= 299) {
        std::cerr << "[AUDIT] logout user=" << userId_ << "\n";
    } else {
        std::cerr << "[AUDIT] logout request failed (HTTP " << resp.statusCode_ << ") — not logged\n";
    }
}

void Auth::changePassword(HttpClient& client,
                          const std::string& baseUrl,
                          const std::string& oldPassword,
                          const std::string& newPassword,
                          const std::string& newWrappedPrivateKey,
                          const std::string& newKekSalt) {
    nlohmann::json body;
    body["old_password"]        = oldPassword;
    body["new_password"]        = newPassword;
    body["wrapped_private_key"] = newWrappedPrivateKey;
    body["kek_salt"]            = newKekSalt;

    const auto resp = client.post(baseUrl + "/auth/change-password", body.dump(),
                                  "application/json", token_);

    if (resp.statusCode_ < 200 || resp.statusCode_ > 299) {
        const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
        throw std::runtime_error("Password change failed (HTTP " +
                                 std::to_string(resp.statusCode_) + "): " + detail);
    }
}

// ── Static factories ─────────────────────────────────────────────────────────

Auth Auth::registerUser(HttpClient& client,
                        const std::string& baseUrl,
                        const std::string& username,
                        const std::string& password,
                        const std::string& email,
                        const std::string& x25519PublicKey,
                        const std::string& wrappedPrivateKey,
                        const std::string& kekSalt) {
    nlohmann::json body;
    body["username"]            = username;
    body["password"]            = password;
    body["email"]               = email;
    body["x25519_public_key"]   = x25519PublicKey;
    body["wrapped_private_key"] = wrappedPrivateKey;
    body["kek_salt"]            = kekSalt;

    const auto resp = client.post(baseUrl + "/auth/register", body.dump());

    if (resp.statusCode_ < 200 || resp.statusCode_ > 299) {
        const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
        throw std::runtime_error("Registration failed (HTTP " +
                                 std::to_string(resp.statusCode_) + "): " +
                                 detail);
    }

    return Auth{};
}

Auth Auth::login(HttpClient& client,
                 const std::string& baseUrl,
                 const std::string& username,
                 const std::string& password) {
    nlohmann::json body;
    body["username"] = username;
    body["password"] = password;

    const auto resp = client.post(baseUrl + "/auth/login", body.dump());

    if (resp.statusCode_ < 200 || resp.statusCode_ > 299) {
        const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
        std::cerr << "[AUDIT] failed_auth username=" << username << "\n";
        throw std::runtime_error("Login failed (HTTP " +
                                 std::to_string(resp.statusCode_) + "): " +
                                 detail);
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        throw std::runtime_error("Login failed: invalid JSON in response");
    }

    if (!parsed.contains("token") || !parsed["token"].is_string()) {
        throw std::runtime_error("Login failed: no token in response");
    }

    Auth auth;
    auth.token_ = parsed["token"].get<std::string>();

    if (parsed.contains("user_id") && parsed["user_id"].is_string())
        auth.userId_ = parsed["user_id"].get<std::string>();

    if (!parsed.contains("wrapped_private_key") || !parsed["wrapped_private_key"].is_string()) {
        throw std::runtime_error("Login failed: no wrapped_private_key in response");
    }
    if (!parsed.contains("kek_salt") || !parsed["kek_salt"].is_string()) {
        throw std::runtime_error("Login failed: no kek_salt in response");
    }
    auth.wrappedPrivateKey_ = parsed["wrapped_private_key"].get<std::string>();
    auth.kekSalt_           = parsed["kek_salt"].get<std::string>();
    std::cerr << "[AUDIT] login user=" << auth.userId_ << "\n";
    return auth;
}
