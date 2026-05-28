#define NOMINMAX
#include <sodium.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "AuthCLI.hpp"
#include "crypto_utils.hpp"
#include "../include/Auth.hpp"
#include "../include/Client.hpp"
#include "../include/Conversation.hpp"
#include "../include/HttpClient.hpp"
#include "../include/MessageStore.hpp"
#include <optional>

static const std::string BASE_URL = "https://localhost:5000";

int main() {
    if (sodium_init() < 0) {
        std::cerr << "\033[1;31m\n        💔 failed to initialise libsodium\n\033[0m\n";
        return 1;
    }

    std::cout << "\033[1;35m"; // bright magenta (pinkish)

    std::cout << R"(

                                    🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀

WhatSaS client starting...

⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣤⣤⣤⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣾⣿⣿⡿⠿⣿⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢰⣿⣿⣿⠋⢀⣠⣾⣿⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⡿⣡⣾⣿⣿⣿⣿⠟⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣰⣿⣷⠿⠟⠛⠛⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⢀⣠⣤⣶⣶⣦⣤⡀⢿⣿⡿⣿⣥⣤⣂⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⣠⣶⣿⣿⣿⠟⠛⠛⣻⣿⣿⣏⠁⠀⠹⣿⣿⣿⣷⣦⣄⠀⠀⠀⠀⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠻⣿⣿⠟⠁⢀⣠⣾⣿⡿⣿⣿⣄⠀⠀⠉⠙⠛⠿⠿⠿⠟⠙⠹⠿⠿⠿⢿⣿⣿⣿⣶⣄⠀⠀⠀
⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⡟⠀⣿⣿⣿⣷⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉⢿⣿⣿⣧⠀⠀
⠀⠀⠀⠀⢻⣿⣿⣿⣿⠟⠁⠀⢸⣿⡿⠛⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⡀⠀
⠀⠀⠀⠀⠀⠉⠚⠋⠀⠀⢀⣠⣾⡟⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣷⠀
⠀⠀⢀⣠⣴⣶⣶⣶⣿⣿⣿⣿⡿⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠹⣿⡟⠋⠓
⠀⣴⣿⠇⠉⠉⠛⠛⠙⠛⠛⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠁⠃⠀⠀
⣴⣿⣿⡆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀    🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀
⢻⣿⣿⣷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀

                ▄▄▄             ▄▄▄  ▄▄▄      ▄▄     ▄▄▄▄▄▄▄    ▄▄▄▄▄       ▄▄      ▄▄▄▄▄
                █▀██  ██  ██▀▀  █▀██  ██     ▄█▀▀█▄  █▀▀██▀▀▀▀  ██▀▀▀▀█▄   ▄█▀▀█▄   ██▀▀▀▀█▄
                ██  ██  ██      ██  ██     ██  ██     ██      ▀██▄  ▄▀   ██  ██   ▀██▄  ▄▀
                ██  ██  ██      ██████     ██▀▀██     ██        ▀██▄▄    ██▀▀██     ▀██▄▄
                ██▄ ██▄ ██      ██  ██   ▄ ██  ██     ██      ▄   ▀██▄ ▄ ██  ██   ▄   ▀██▄
                ▀████▀███▀    ▀██▀  ▀██▄ ▀██▀  ▀█▄█   ▀██▄    ▀██████▀ ▀██▀  ▀█▄█ ▀██████▀

                                        🎀  W H A T S A S  🎀

                    💌 secure chats • cute vibes 💌
                    💖 loading encryption modules... 💖

                    🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀

)";

    std::cout << R"(
        ┌──────────────────────────────────────────┐
        │                                          │
        │   💌  welcome to whatsas!                │
        │                                          │
        │      [1]  🌸  register                   │
        │      [2]  💖  login                      │
        │                                          │
        └──────────────────────────────────────────┘

)";

    std::cout << "        ✨ your choice: ";
    std::cout << "\033[0m";

    std::string choice;
    while (true) {
        if (!std::getline(std::cin, choice)) {
            std::cerr << "\033[1;31m\n        💔 input stream closed unexpectedly\n\033[0m\n";
            return 1;
        }
        if (choice == "1" || choice == "2") break;
        std::cout << "\033[1;35m        💔 please enter 1 or 2: \033[0m";
    }

    std::cout << "\033[1;35m\n";
    if (choice == "1") {
        std::cout << "        🌸 let's get you registered!\n\n\033[0m";
    } else {
        std::cout << "        💖 welcome back!\n\n\033[0m";
    }

    const auto creds = promptCredentials();

    HttpClient http;
    Auth auth;
    std::optional<Client> client;
    try {
        if (choice == "1") {
            if (!crypto_aead_aes256gcm_is_available()) {
                throw std::runtime_error("AES-256-GCM requires hardware AES-NI — unavailable on this CPU");
            }

            std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
            std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
            crypto_box_keypair(pk.data(), sk.data());

            std::vector<uint8_t> kekSalt(crypto_pwhash_SALTBYTES);
            randombytes_buf(kekSalt.data(), kekSalt.size());

            std::vector<uint8_t> kek(32);
            if (crypto_pwhash(
                    kek.data(), kek.size(),
                    creds.password.c_str(), creds.password.size(),
                    kekSalt.data(),
                    crypto_pwhash_OPSLIMIT_INTERACTIVE,
                    crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
                throw std::runtime_error("key derivation failed — not enough memory for Argon2id");
            }

            // wrapped layout: nonce (12) || ciphertext+tag (32+16 = 48)
            unsigned char wrapNonce[crypto_aead_aes256gcm_NPUBBYTES];
            randombytes_buf(wrapNonce, sizeof(wrapNonce));

            std::vector<uint8_t> wrapped(
                sizeof(wrapNonce) + crypto_box_SECRETKEYBYTES + crypto_aead_aes256gcm_ABYTES);
            unsigned long long wrappedCtLen = 0;
            if (crypto_aead_aes256gcm_encrypt(
                    wrapped.data() + sizeof(wrapNonce), &wrappedCtLen,
                    sk.data(), sk.size(),
                    nullptr, 0,
                    nullptr,
                    wrapNonce, kek.data()) != 0) {
                throw std::runtime_error("private key wrapping failed");
            }
            std::copy(wrapNonce, wrapNonce + sizeof(wrapNonce), wrapped.begin());
            wrapped.resize(sizeof(wrapNonce) + wrappedCtLen);

            auth = Auth::registerUser(http, BASE_URL,
                                      creds.username, creds.password,
                                      creds.email,
                                      b64Encode(pk.data(), pk.size()),
                                      b64Encode(wrapped.data(), wrapped.size()),
                                      b64Encode(kekSalt.data(), kekSalt.size()));

            // Registration returns no token — log in immediately to get one.
            // sk and pk are already in scope so there is no need to re-derive them.
            auth = Auth::login(http, BASE_URL, creds.username, creds.password);
            client.emplace(BASE_URL, creds.username,
                           std::move(sk), std::move(pk),
                           "whatsas_pins.txt", auth.getToken());

            std::cout << "\033[1;35m\n        🌸 registered! welcome to whatsas, " << creds.username << "~ 💖\n";
        } else {
            auth = Auth::login(http, BASE_URL, creds.username, creds.password);

            const std::vector<uint8_t> kekSaltBytes = b64Decode(auth.getKekSalt());
            if (kekSaltBytes.size() != crypto_pwhash_SALTBYTES) {
                throw std::runtime_error("login response: kek_salt is missing or malformed");
            }

            std::vector<uint8_t> kek(32);
            if (crypto_pwhash(
                    kek.data(), kek.size(),
                    creds.password.c_str(), creds.password.size(),
                    kekSaltBytes.data(),
                    crypto_pwhash_OPSLIMIT_INTERACTIVE,
                    crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
                throw std::runtime_error("key derivation failed — not enough memory for Argon2id");
            }

            if (!crypto_aead_aes256gcm_is_available()) {
                throw std::runtime_error("AES-256-GCM requires hardware AES-NI — unavailable on this CPU");
            }

            const std::vector<uint8_t> wrappedBytes = b64Decode(auth.getWrappedPrivateKey());
            constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
            constexpr std::size_t tagLen   = crypto_aead_aes256gcm_ABYTES;
            if (wrappedBytes.size() != nonceLen + crypto_box_SECRETKEYBYTES + tagLen) {
                throw std::runtime_error("login response: wrapped_private_key is malformed");
            }

            std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
            unsigned long long skLen = 0;
            if (crypto_aead_aes256gcm_decrypt(
                    sk.data(), &skLen,
                    nullptr,
                    wrappedBytes.data() + nonceLen, wrappedBytes.size() - nonceLen,
                    nullptr, 0,
                    wrappedBytes.data(),
                    kek.data()) != 0) {
                throw std::runtime_error("private key unwrapping failed — wrong password or corrupted data");
            }
            sk.resize(skLen);

            std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
            if (crypto_scalarmult_base(pk.data(), sk.data()) != 0) {
                throw std::runtime_error("public key derivation failed — private key may be corrupted");
            }

            client.emplace(BASE_URL, creds.username,
                           std::move(sk), std::move(pk),
                           "whatsas_pins.txt", auth.getToken());

            std::cout << "\033[1;35m\n        💖 logged in! welcome back, " << creds.username << "~ 🎀\n";
        }

        std::cout << "        🔑 session token received ✅\n\033[0m\n";

    } catch (const std::exception& e) {
        std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m\n";
        return 1;
    }

    if (!client.has_value()) return 0;  // registration complete — no session yet

    if (choice == "2") {
        // Registration already sends x25519_public_key in the register request body.
        // Only publish on login, where the key registry may not yet have an entry.
        const auto pubResp = client->publishPublicKey(creds.username, client->getPublicKey());
        if (pubResp.statusCode_ < 200 || pubResp.statusCode_ > 299) {
            std::cerr << "\033[1;33m\n        ⚠  key publish failed (HTTP "
                      << pubResp.statusCode_ << ") — peers may not be able to find you\n\033[0m\n";
        }
    }

    while (true) {
        std::cout << "\033[1;35m" << R"(
        ┌──────────────────────────────────────────┐
        │                                          │
        │   💌  what would you like to do?         │
        │                                          │
        │      [1]  📨  send a message             │
        │      [2]  💬  view messages              │
        │      [3]  👋  quit                       │
        │                                          │
        └──────────────────────────────────────────┘

)" << "        ✨ your choice: \033[0m";

        std::string action;
        if (!std::getline(std::cin, action)) break;
        if (action == "3") break;
        if (action != "1" && action != "2") {
            std::cout << "\033[1;35m        💔 please enter 1, 2, or 3\033[0m\n";
            continue;
        }

        // ── shared: prompt for peer username and fetch their public key ──────────
        std::cout << "\033[1;35m\n        👤 peer username: \033[0m";
        std::string peerId;
        if (!std::getline(std::cin, peerId) || peerId.empty()) {
            std::cout << "\033[1;35m        💔 username cannot be empty\033[0m\n";
            continue;
        }

        const auto peerPk = client->fetchPeerPublicKey(peerId);
        if (peerPk.empty()) {
            std::cout << "\033[1;31m\n        💔 could not fetch public key for '"
                      << peerId << "' — user not found or key substitution detected\033[0m\n";
            continue;
        }

        if (action == "1") {
            std::cout << "\033[1;35m        💬 your message: \033[0m";
            std::string text;
            if (!std::getline(std::cin, text) || text.empty()) {
                std::cout << "\033[1;35m        💔 message cannot be empty\033[0m\n";
                continue;
            }

            const auto resp = client->sendMessage(peerId, peerPk, text);
            if (resp.statusCode_ >= 200 && resp.statusCode_ <= 299) {
                std::cout << "\033[1;35m\n        💌 message sent! ✅\033[0m\n";
            } else {
                const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
                std::cout << "\033[1;31m\n        💔 send failed (HTTP "
                          << resp.statusCode_ << "): " << detail << "\033[0m\n";
            }
        } else {
            MessageStore store;
            Conversation conv(peerId);

            const int count = client->receiveMessages(store, conv, peerPk);
            if (count < 0) {
                std::cout << "\033[1;31m\n        💔 failed to fetch messages from server\033[0m\n";
                continue;
            }

            std::cout << "\033[1;35m\n        💬 conversation with " << peerId << ":\n\033[0m\n";
            printConversation(conv);
            std::cout << "\033[1;35m\n        (" << count << " message(s) received)\033[0m\n";
        }
    }

    std::cout << "\033[1;35m\n        💖 bye bye! ✨\n\033[0m\n";
    return 0;
}
