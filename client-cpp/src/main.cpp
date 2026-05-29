#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include "AuthCLI.hpp"
#include "crypto_utils.hpp"
#include "ui.hpp"
#include "../include/Auth.hpp"
#include "../include/Client.hpp"
#include "../include/Conversation.hpp"
#include "../include/HttpClient.hpp"
#include "../include/MessageStore.hpp"

static const std::string BASE_URL = "https://sas.theburkenator.com";

static void runMockFlow() {
    showBanner();
    const std::string username = "testuser";
    std::cout << "\033[1;35m\n        💖 mock mode — skipping auth, welcome " << username << "~ 🎀\n\033[0m\n";

    while (true) {
        const MainChoice action = showMainMenu(username);
        if (action == MainChoice::Eof || action == MainChoice::Logout) break;
        if (action == MainChoice::ChangePassword) {
            std::cout << "\033[1;35m\n        🔒 mock mode — password change not available\n\033[0m\n";
            continue;
        }

        const std::string peerId = promptPeer();
        if (peerId.empty()) continue;

        if (action == MainChoice::Send) {
            const std::string text = promptMessage(peerId);
            if (text.empty()) continue;
            showSendResult(true, 200, "");
        } else {
            while (true) {
                // build a fake conversation with some sample messages
                Conversation conv(peerId);
                std::time_t now = std::time(nullptr);
                conv.addMessage({"id1", peerId,   username, "hey! how are you?",     now - 120});
                conv.addMessage({"id2", username, peerId,   "doing great, you?",     now - 60});
                conv.addMessage({"id3", peerId,   username, "same! 🎀",              now - 30});

                showConversation(conv, username, 3);

                const ConvChoice convAction = showConversationMenu();
                if (convAction == ConvChoice::Back || convAction == ConvChoice::Eof) break;

                const std::string text = promptMessage(peerId);
                if (text.empty()) continue;
                showSendResult(true, 200, "");
            }
        }
    }

    showGoodbye();
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--mock") {
            runMockFlow();
            return 0;
        }
    }

    if (sodium_init() < 0) {
        std::cerr << "\033[1;31m\n        💔 failed to initialise libsodium\n\033[0m\n";
        return 1;
    }

    showBanner();

    // ── auth ──────────────────────────────────────────────────────────────────
    const AuthChoice choice = showAuthMenu();
    if (choice == AuthChoice::Eof) return 1;

    std::cout << "\033[1;35m\n";
    if (choice == AuthChoice::Register) {
        std::cout << "        🌸 let's get you registered!\n\n\033[0m";
    } else {
        std::cout << "        💖 welcome back!\n\n\033[0m";
    }

    HttpClient http;
    Auth auth;
    std::optional<Client> client;
    std::string username;
    std::vector<uint8_t> sessionSk;  // kept alongside client for password re-wrap

    try {
        if (choice == AuthChoice::Register) {
            const auto creds = promptCredentials();
            if (!crypto_aead_aes256gcm_is_available())
                throw std::runtime_error("AES-256-GCM requires hardware AES-NI — unavailable on this CPU");

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
                    crypto_pwhash_ALG_ARGON2ID13) != 0)
                throw std::runtime_error("key derivation failed — not enough memory for Argon2id");

            unsigned char wrapNonce[crypto_aead_aes256gcm_NPUBBYTES];
            randombytes_buf(wrapNonce, sizeof(wrapNonce));

            std::vector<uint8_t> wrapped(
                sizeof(wrapNonce) + crypto_box_SECRETKEYBYTES + crypto_aead_aes256gcm_ABYTES);
            unsigned long long wrappedCtLen = 0;
            if (crypto_aead_aes256gcm_encrypt(
                    wrapped.data() + sizeof(wrapNonce), &wrappedCtLen,
                    sk.data(), sk.size(),
                    nullptr, 0, nullptr,
                    wrapNonce, kek.data()) != 0)
                throw std::runtime_error("private key wrapping failed");

            std::copy(wrapNonce, wrapNonce + sizeof(wrapNonce), wrapped.begin());
            wrapped.resize(sizeof(wrapNonce) + wrappedCtLen);

            auth = Auth::registerUser(http, BASE_URL,
                                      creds.username, creds.password, creds.email,
                                      b64Encode(pk.data(), pk.size()),
                                      b64Encode(wrapped.data(), wrapped.size()),
                                      b64Encode(kekSalt.data(), kekSalt.size()));

            auth = Auth::login(http, BASE_URL, creds.username, creds.password);
            sessionSk = sk;
            client.emplace(BASE_URL, creds.username,
                           std::move(sk), std::move(pk),
                           "whatsas_pins.txt", auth.getToken());

            username = creds.username;
            std::cout << "\033[1;35m\n        🌸 registered! welcome to whatsas, " << username << "~ 💖\n";

        } else {
            const auto creds = promptLogin();
            auth = Auth::login(http, BASE_URL, creds.username, creds.password);

            const std::vector<uint8_t> kekSaltBytes = b64Decode(auth.getKekSalt());
            if (kekSaltBytes.size() != crypto_pwhash_SALTBYTES)
                throw std::runtime_error(
                    "login response: kek_salt must be " +
                    std::to_string(crypto_pwhash_SALTBYTES) +
                    " bytes, got " + std::to_string(kekSaltBytes.size()));

            const std::vector<uint8_t> wrappedBytes = b64Decode(auth.getWrappedPrivateKey());
            constexpr std::size_t expectedWrappedLen =
                crypto_aead_aes256gcm_NPUBBYTES +
                crypto_box_SECRETKEYBYTES +
                crypto_aead_aes256gcm_ABYTES;
            if (wrappedBytes.size() != expectedWrappedLen)
                throw std::runtime_error(
                    "login response: wrapped_private_key must be " +
                    std::to_string(expectedWrappedLen) +
                    " bytes, got " + std::to_string(wrappedBytes.size()));

            if (!crypto_aead_aes256gcm_is_available())
                throw std::runtime_error("AES-256-GCM requires hardware AES-NI — unavailable on this CPU");

            std::vector<uint8_t> kek(32);
            if (crypto_pwhash(
                    kek.data(), kek.size(),
                    creds.password.c_str(), creds.password.size(),
                    kekSaltBytes.data(),
                    crypto_pwhash_OPSLIMIT_INTERACTIVE,
                    crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_ARGON2ID13) != 0)
                throw std::runtime_error("key derivation failed — not enough memory for Argon2id");

            std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
            unsigned long long skLen = 0;
            if (crypto_aead_aes256gcm_decrypt(
                    sk.data(), &skLen,
                    nullptr,
                    wrappedBytes.data() + crypto_aead_aes256gcm_NPUBBYTES,
                    wrappedBytes.size() - crypto_aead_aes256gcm_NPUBBYTES,
                    nullptr, 0,
                    wrappedBytes.data(),
                    kek.data()) != 0)
                throw std::runtime_error("private key unwrapping failed — wrong password or corrupted data");
            sk.resize(skLen);

            std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
            if (crypto_scalarmult_base(pk.data(), sk.data()) != 0)
                throw std::runtime_error("public key derivation failed — private key may be corrupted");

            sessionSk = sk;
            client.emplace(BASE_URL, creds.username,
                           std::move(sk), std::move(pk),
                           "whatsas_pins.txt", auth.getToken());

            username = creds.username;
            std::cout << "\033[1;35m\n        💖 logged in! welcome back, " << username << "~ 🎀\n";
        }

        std::cout << "        🔑 session token received ✅\n\033[0m\n";

    } catch (const std::exception& e) {
        std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m\n";
        return 1;
    }

    if (!client.has_value()) return 0;

    // publish key on every login/register so the registry stays current
    {
        const auto pubResp = client->publishPublicKey(auth.getUserId(), client->getPublicKey());
        if (pubResp.statusCode_ < 200 || pubResp.statusCode_ > 299) {
            std::cerr << "\033[1;33m\n        ⚠  key publish failed (HTTP "
                      << pubResp.statusCode_ << ") — peers may not be able to find you\n\033[0m\n";
        }
    }

    std::cout << "\033[1;35m        🔑 your key fingerprint: "
              << keyFingerprint(client->getPublicKey())
              << "\n        share this with contacts to verify your identity out-of-band\n\033[0m\n";

    // ── main loop ─────────────────────────────────────────────────────────────
    while (true) {
        const MainChoice action = showMainMenu(username);
        if (action == MainChoice::Eof || action == MainChoice::Logout) {
            auth.logout(http, BASE_URL);
            std::cout << "\033[1;35m\n        🚪 logged out — see you soon, " << username << "~ 💖\n\033[0m\n";
            showGoodbye();
            return 0;
        }

        if (action == MainChoice::ChangePassword) {
            const auto pwChange = promptPasswordChange();
            if (pwChange.oldPassword.empty()) continue;

            try {
                // Derive new KEK from new password with a fresh salt
                std::vector<uint8_t> newKekSalt(crypto_pwhash_SALTBYTES);
                randombytes_buf(newKekSalt.data(), newKekSalt.size());

                std::vector<uint8_t> newKek(32);
                if (crypto_pwhash(
                        newKek.data(), newKek.size(),
                        pwChange.newPassword.c_str(), pwChange.newPassword.size(),
                        newKekSalt.data(),
                        crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE,
                        crypto_pwhash_ALG_ARGON2ID13) != 0)
                    throw std::runtime_error("key derivation failed — not enough memory for Argon2id");

                // Re-wrap the private key under the new KEK
                unsigned char wrapNonce[crypto_aead_aes256gcm_NPUBBYTES];
                randombytes_buf(wrapNonce, sizeof(wrapNonce));

                std::vector<uint8_t> newWrapped(
                    sizeof(wrapNonce) + crypto_box_SECRETKEYBYTES + crypto_aead_aes256gcm_ABYTES);
                unsigned long long wrappedCtLen = 0;
                if (crypto_aead_aes256gcm_encrypt(
                        newWrapped.data() + sizeof(wrapNonce), &wrappedCtLen,
                        sessionSk.data(), sessionSk.size(),
                        nullptr, 0, nullptr,
                        wrapNonce, newKek.data()) != 0)
                    throw std::runtime_error("private key re-wrapping failed");

                std::copy(wrapNonce, wrapNonce + sizeof(wrapNonce), newWrapped.begin());
                newWrapped.resize(sizeof(wrapNonce) + wrappedCtLen);

                auth.changePassword(http, BASE_URL,
                                    pwChange.oldPassword, pwChange.newPassword,
                                    b64Encode(newWrapped.data(), newWrapped.size()),
                                    b64Encode(newKekSalt.data(), newKekSalt.size()));

                std::cout << "\033[1;35m\n        🔒 password changed successfully! 💖\n\033[0m\n";
            } catch (const std::exception& e) {
                std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m\n";
            }
            continue;
        }

        // both send and view need a peer
        const std::string peerId = promptPeer();
        if (peerId.empty()) continue;

        const auto peerPk = client->fetchPeerPublicKey(peerId);
        if (peerPk.empty()) {
            std::cout << "\033[1;31m\n        💔 could not find '" << peerId
                      << "' — user not found or key substitution detected\033[0m\n";
            continue;
        }

        std::cout << "\033[1;35m        🔑 " << peerId << "'s key fingerprint: "
                  << keyFingerprint(peerPk)
                  << "\n        verify this matches what " << peerId << " sees for themselves\n\033[0m\n";

        if (action == MainChoice::Send) {
            // ── send ──────────────────────────────────────────────────────────
            const std::string text = promptMessage(peerId);
            if (text.empty()) continue;

            const auto resp = client->sendMessage(peerId, peerPk, text);
            const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
            showSendResult(resp.statusCode_ >= 200 && resp.statusCode_ <= 299,
                           resp.statusCode_, detail);

        } else {
            // ── view conversation (with optional reply loop) ───────────────────
            while (true) {
                MessageStore store;
                Conversation conv(peerId);

                const int count = client->receiveMessages(store, conv, peerPk);
                if (count < 0) {
                    std::cout << "\033[1;31m\n        💔 failed to fetch messages from server\033[0m\n";
                    break;
                }

                showConversation(conv, username, count);

                const ConvChoice convAction = showConversationMenu();
                if (convAction == ConvChoice::Back || convAction == ConvChoice::Eof) break;

                // reply
                const std::string text = promptMessage(peerId);
                if (text.empty()) continue;

                const auto resp = client->sendMessage(peerId, peerPk, text);
                const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
                showSendResult(resp.statusCode_ >= 200 && resp.statusCode_ <= 299,
                               resp.statusCode_, detail);
            }
        }
    }

    showGoodbye();
    return 0;
}
