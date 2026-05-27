#include "../include/Auth.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

// ── Accessors ────────────────────────────────────────────────────────────────

const std::string& Auth::getToken() const {
    return token_;
}

bool Auth::isLoggedIn() const {
    return !token_.empty();
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
    return auth;
}
