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
                        const std::string& password) {
    nlohmann::json body;
    body["username"] = username;
    body["password"] = password;

    const auto resp = client.post(baseUrl + "/auth/register", body.dump());

    if (!resp.ok_) {
        throw std::runtime_error("Registration failed (HTTP " +
                                 std::to_string(resp.statusCode_) + "): " +
                                 resp.error_);
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(resp.body_);
    } catch (const nlohmann::json::parse_error&) {
        throw std::runtime_error("Registration failed: invalid JSON in response");
    }

    if (!parsed.contains("token") || !parsed["token"].is_string()) {
        throw std::runtime_error("Registration failed: no token in response");
    }

    Auth auth;
    auth.token_ = parsed["token"].get<std::string>();
    return auth;
}

Auth Auth::login(HttpClient& client,
                 const std::string& baseUrl,
                 const std::string& username,
                 const std::string& password) {
    nlohmann::json body;
    body["username"] = username;
    body["password"] = password;

    const auto resp = client.post(baseUrl + "/auth/login", body.dump());

    if (!resp.ok_) {
        throw std::runtime_error("Login failed (HTTP " +
                                 std::to_string(resp.statusCode_) + "): " +
                                 resp.error_);
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
