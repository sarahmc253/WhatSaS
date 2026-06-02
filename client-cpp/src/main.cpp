#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
// CPUID: include the right header per compiler; fall back silently on unknown toolchains.
#if defined(_MSC_VER)
#  include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#  include <cpuid.h>
#endif
#include "AuthCLI.hpp"
#include "crypto_utils.hpp"
#include <regex>
#include "key_wrap.hpp"
#include "ui.hpp"
#include "../include/Auth.hpp"
#include "../include/Client.hpp"
#include "../include/Conversation.hpp"
#include "../include/HttpClient.hpp"
#include "../include/MessageStore.hpp"

static const std::string BASE_URL = "https://sas.theburkenator.com";

// Returns true if the CPU advertises AES-NI support (CPUID leaf 1, ECX bit 25).
// Falls back to true on toolchains that expose neither __get_cpuid nor __cpuid so
// that unsupported builds fail at the libsodium call site rather than here.
static bool hasAesNi() {
#if defined(_MSC_VER)
    int info[4] = {};
    __cpuid(info, 1);
    return (info[2] >> 25) & 1;
#elif defined(__GNUC__) || defined(__clang__)
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
    return (c >> 25) & 1;
#else
    return true;
#endif
}

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

    static const std::regex usernameRe("^[A-Za-z0-9_]{3,32}$");
    auto validUsername = [&](const std::string& u) -> bool {
        if (std::regex_match(u, usernameRe)) return true;
        std::cout << "\033[1;33m\n        ⚠  username must be 3–32 characters: letters, numbers, and underscores only\n\033[0m";
        return false;
    };
    auto validPassword = [](const std::string& p) -> bool {
        if (p.size() >= 8) return true;
        std::cout << "\033[1;33m\n        ⚠  password must be at least 8 characters\n\033[0m";
        return false;
    };

    if (!hasAesNi()) {
        std::cerr << "\033[1;31m\n        💔 AES-256-GCM requires hardware AES-NI — unavailable on this CPU\n\033[0m\n";
        return 1;
    }

    while (!client.has_value()) {
        try {
            if (choice == AuthChoice::Register) {
                Credentials creds;
                do { std::cout << "Username: "; std::getline(std::cin, creds.username); } while (!validUsername(creds.username));
                do { creds.password = readPassword(); } while (!validPassword(creds.password));

                // Two-layer key wrapping (KEK→DEK→SK) matching the web client and spec §3.6
                const auto kp      = generateX25519Keypair();
                const auto wrapped = wrapPrivateKey(kp, creds.password);

                auth = Auth::registerUser(http, BASE_URL,
                                          creds.username, creds.password,
                                          wrapped.x25519PublicKeyB64,
                                          wrapped.wrappedPrivateKeyB64,
                                          wrapped.kekSaltB64);

                auth = Auth::login(http, BASE_URL, creds.username, creds.password);
                sessionSk = kp.privateKey;

                std::vector<uint8_t> pk(kp.publicKey);
                std::vector<uint8_t> sk(kp.privateKey);
                client.emplace(BASE_URL, auth.getUserId(),
                               std::move(sk), std::move(pk),
                               "whatsas_pins.txt", auth.getToken());

                username = creds.username;
                std::cout << "\033[1;35m\n        🌸 registered! welcome to whatsas, " << username << "~ 💖\n";

            } else {
                LoginCredentials creds;
                std::cout << "Username: "; std::getline(std::cin, creds.username);
                creds.password = readPassword();

                auth = Auth::login(http, BASE_URL, creds.username, creds.password);

                // Two-layer unwrap (KEK→DEK→SK), handles both PKCS8 (web) and raw (old C++) inner formats
                auto sk = unwrapPrivateKey(
                    auth.getWrappedPrivateKey(), auth.getKekSalt(), creds.password);

                std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
                if (crypto_scalarmult_base(pk.data(), sk.data()) != 0)
                    throw std::runtime_error("public key derivation failed — private key may be corrupted");

                sessionSk = sk;
                client.emplace(BASE_URL, auth.getUserId(),
                               std::move(sk), std::move(pk),
                               "whatsas_pins.txt", auth.getToken());

                username = creds.username;
                std::cout << "\033[1;35m\n        💖 logged in! welcome back, " << username << "~ 🎀\n";
            }

            std::cout << "        🔑 session token received ✅\n\033[0m\n";

        } catch (const std::exception& e) {
            std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m";
            std::cout << "\033[1;33m        ↩  try again\n\033[0m\n";
        }
    }

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
            MessageStore store;
            Conversation conv(peerId);

            while (true) {
                // Re-fetch from server each iteration to pick up new incoming messages.
                // Sent messages are injected into conv at send time below, so they
                // survive across iterations without needing to re-decrypt.
                Conversation freshConv(peerId);
                const int count = client->receiveMessages(store, freshConv, peerPk);
                if (count < 0) {
                    std::cout << "\033[1;31m\n        💔 failed to fetch messages from server\033[0m\n";
                    break;
                }
                // Merge fresh received messages into conv (preserves locally-injected sent msgs).
                for (const auto& dm : freshConv.getMessages()) {
                    conv.addMessage(dm);
                }

                showConversation(conv, username, count);

                const ConvChoice convAction = showConversationMenu();
                if (convAction == ConvChoice::Back || convAction == ConvChoice::Eof) break;

                // reply
                const std::string text = promptMessage(peerId);
                if (text.empty()) continue;

                const auto resp = client->sendMessage(peerId, peerPk, text);
                const bool ok = resp.statusCode_ >= 200 && resp.statusCode_ <= 299;
                const std::string detail = !resp.error_.empty() ? resp.error_ : resp.body_;
                showSendResult(ok, resp.statusCode_, detail);

                // Sent message will appear as "[message sent]" on next loop refresh
                // via the server's direction:"sent" placeholder.
            }
        }
    }

    showGoodbye();
    return 0;
}
