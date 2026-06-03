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
#include <fstream>
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
    const std::string username = "testuser";
    showInfo("mock mode — welcome " + username + "~ 🎀");

    while (true) {
        const MainChoice action = showMainMenu(username);
        if (action == MainChoice::Eof || action == MainChoice::Logout) break;
        if (action == MainChoice::ChangePassword) {
            showInfo("mock mode — password change not available");
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

                showConversation(conv, username, 3, "mock-fingerprint", "mock-fingerprint");

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
        showError("failed to initialise libsodium");
        return 1;
    }

    // ── auth ──────────────────────────────────────────────────────────────────
    const AuthChoice choice = showAuthMenu();
    if (choice == AuthChoice::Eof) return 1;

    HttpClient http;
    Auth auth;
    std::optional<Client> client;
    std::string username;
    std::vector<uint8_t> sessionSk;  // kept alongside client for password re-wrap

    static const std::regex usernameRe("^[A-Za-z0-9_]{3,32}$");
    auto validUsername = [&](const std::string& u) -> bool {
        if (std::regex_match(u, usernameRe)) return true;
        showWarning("username must be 3–32 characters: letters, numbers, and underscores only");
        return false;
    };
    auto validPassword = [](const std::string& p) -> bool {
        if (p.size() >= 8) return true;
        showWarning("password must be at least 8 characters");
        return false;
    };

    if (!hasAesNi()) {
        showError("AES-256-GCM requires hardware AES-NI — unavailable on this CPU");
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
                showSuccess("registered! welcome to whatsas, " + username + "~ 💖");

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
                showSuccess("logged in! welcome back, " + username + "~ 🎀");
            }


        } catch (const std::exception& e) {
            showError(e.what()); showRetry();
        }
    }

    showFingerprint("your fingerprint", keyFingerprint(client->getPublicKey()));

    // ── main loop ─────────────────────────────────────────────────────────────
    while (true) {
        const MainChoice action = showMainMenu(username);
        if (action == MainChoice::Eof || action == MainChoice::Logout) {
            auth.logout(http, BASE_URL);
            showInfo("logged out — see you soon, " + username + "~ 💖");
            showGoodbye();
            return 0;
        }

        if (action == MainChoice::SwitchUser) {
            auth.logout(http, BASE_URL);
            showInfo("switching user — bye " + username + "~ 💖");

            client.reset();
            username.clear();
            sessionSk.clear();

            const AuthChoice switchChoice = showAuthMenu();
            if (switchChoice == AuthChoice::Eof) { showGoodbye(); return 0; }

            while (!client.has_value()) {
                try {
                    if (switchChoice == AuthChoice::Register) {
                        Credentials creds;
                        do { std::cout << "Username: "; std::getline(std::cin, creds.username); } while (!validUsername(creds.username));
                        do { creds.password = readPassword(); } while (!validPassword(creds.password));
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
                        client.emplace(BASE_URL, auth.getUserId(), std::move(sk), std::move(pk),
                                       "whatsas_pins.txt", auth.getToken());
                        username = creds.username;
                        showSuccess("registered! welcome to whatsas, " + username + "~ 💖");
                    } else {
                        LoginCredentials creds;
                        std::cout << "Username: "; std::getline(std::cin, creds.username);
                        creds.password = readPassword();
                        auth = Auth::login(http, BASE_URL, creds.username, creds.password);
                        auto sk = unwrapPrivateKey(auth.getWrappedPrivateKey(), auth.getKekSalt(), creds.password);
                        std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
                        if (crypto_scalarmult_base(pk.data(), sk.data()) != 0)
                            throw std::runtime_error("public key derivation failed — private key may be corrupted");
                        sessionSk = sk;
                        client.emplace(BASE_URL, auth.getUserId(), std::move(sk), std::move(pk),
                                       "whatsas_pins.txt", auth.getToken());
                        username = creds.username;
                        showSuccess("logged in! welcome back, " + username + "~ 🎀");
                    }
                        } catch (const std::exception& e) {
                    std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m";
                    std::cout << "\033[1;33m        ↩  try again\n\033[0m\n";
                }
            }

            std::cout << "\033[1;35m            🔑 your key fingerprint: "
                      << keyFingerprint(client->getPublicKey())
                      << "\n            share this with contacts to verify your identity out-of-band\n\033[0m\n";
            continue;
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

                showSuccess("password changed successfully! 💖");
            } catch (const std::exception& e) {
                showError(e.what());
            }
            continue;
        }

        // both send and view need a peer
        const std::string peerId = promptPeer();
        if (peerId.empty()) continue;

        const auto peerPk = client->fetchPeerPublicKey(peerId);
        if (peerPk.empty()) {
            showUserNotFound(peerId);
            continue;
        }


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
                clearScreen();
                // Rebuild conv fresh each time so deleted/revoked messages disappear.
                store = MessageStore{};
                conv  = Conversation{peerId};
                const int count = client->receiveMessages(store, conv, peerPk);
                if (count < 0) {
                    std::cout << C_DANGER "\n  💔 failed to fetch messages from server\n" C_RESET;
                    break;
                }

                showConversation(conv, username, count,
                                 keyFingerprint(client->getPublicKey()),
                                 keyFingerprint(peerPk));

                const ConvChoice convAction = showConversationMenu();
                if (convAction == ConvChoice::Back || convAction == ConvChoice::Eof) break;
                if (convAction == ConvChoice::Refresh) continue;

                if (convAction == ConvChoice::MessageAction) {
                    const auto msgs = conv.getMessages();
                    if (msgs.empty()) {
                        showInfo("no messages to act on");
                        pauseForUser();
                        continue;
                    }

                    std::cout << C_MUTED "\n  message #: " C_RESET;  // kept inline — single prompt
                    std::string numStr;
                    if (!std::getline(std::cin, numStr)) continue;
                    int msgIdx = 0;
                    try { msgIdx = std::stoi(numStr) - 1; } catch (...) { continue; }
                    if (msgIdx < 0 || msgIdx >= static_cast<int>(msgs.size())) {
                        std::cout << C_DANGER "  invalid number\n" C_RESET;
                        pauseForUser();
                        continue;
                    }
                    const auto& sel = msgs[msgIdx];
                    const bool isMine    = (sel.senderId == username);
                    const bool hasPlain  = (sel.plaintext != "[message sent]" && sel.plaintext != "[revoked]");

                    showMessageActionMenu(msgIdx + 1, sel.plaintext, isMine, hasPlain);

                    std::string act;
                    if (!std::getline(std::cin, act)) continue;

                    if (act == "1") {
                        const auto r = client->deleteMessage(sel.messageId);
                        std::cout << (r.statusCode_ >= 200 && r.statusCode_ <= 299
                            ? C_SUCCESS "\n  ✓  deleted\n" C_RESET
                            : C_DANGER  "\n  ✗  delete failed\n" C_RESET);
                        pauseForUser();

                    } else if (act == "2" && isMine) {
                        const auto r = client->revokeMessage(sel.messageId);
                        std::cout << (r.statusCode_ >= 200 && r.statusCode_ <= 299
                            ? C_SUCCESS "\n  ✓  revoked\n" C_RESET
                            : C_DANGER  "\n  ✗  revoke failed\n" C_RESET);
                        pauseForUser();

                    } else if (act == "3") {
                        std::cout << C_MUTED "\n  forward to: " C_RESET;
                        std::string fwdPeer;
                        if (!std::getline(std::cin, fwdPeer) || fwdPeer.empty()) continue;
                        const auto fwdPk = client->fetchPeerPublicKey(fwdPeer);
                        if (fwdPk.empty()) {
                            std::cout << C_DANGER "  ✗  user not found\n" C_RESET;
                        } else {
                            const auto r = client->forwardMessage(sel.messageId, fwdPeer, fwdPk, sel.plaintext);
                            std::cout << (r.statusCode_ >= 200 && r.statusCode_ <= 299
                                ? C_SUCCESS "\n  ✓  forwarded\n" C_RESET
                                : C_DANGER  "\n  ✗  forward failed: " + r.body_ + "\n" C_RESET);
                        }
                        pauseForUser();

                    } else if (act == "4" && hasPlain) {
                        const auto r = client->getMessage(sel.messageId);
                        if (r.statusCode_ >= 200 && r.statusCode_ <= 299) {
                            const std::string fname = "msg_" + sel.messageId.substr(0, 8) + ".txt";
                            std::ofstream f(fname);
                            if (f) {
                                try {
                                    auto j = nlohmann::json::parse(r.body_);
                                    auto field = [&](const char* k) -> std::string {
                                        return j.contains(k) && !j[k].is_null() ? j[k].get<std::string>() : "(unknown)";
                                    };
                                    std::string tsStr = "(unknown)";
                                    if (j.contains("timestamp") && j["timestamp"].is_number_integer()) {
                                        std::time_t ts = j["timestamp"].get<long long>();
                                        char tbuf[32];
                                        if (std::tm* t = std::localtime(&ts))
                                            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", t);
                                        tsStr = tbuf;
                                    }
                                    std::string senderUser = sel.senderId;
                                    std::string recipUser  = sel.recipientId.empty() ? peerId : sel.recipientId;
                                    if (senderUser.size() == 36 && senderUser[8] == '-') senderUser = field("sender_username");
                                    if (recipUser.size()  == 36 && recipUser[8]  == '-') recipUser  = field("recipient_username");
                                    f << "From : " << senderUser << "\n"
                                      << "To   : " << recipUser  << "\n"
                                      << "Date : " << tsStr      << "\n\n"
                                      << sel.plaintext << "\n";
                                } catch (...) { f << r.body_; }
                                std::cout << C_SUCCESS "\n  ✓  saved to " << fname << "\n" C_RESET;
                            } else {
                                std::cout << C_DANGER "\n  ✗  could not write file\n" C_RESET;
                            }
                        } else {
                            std::cout << C_DANGER "\n  ✗  download failed\n" C_RESET;
                        }
                        pauseForUser();
                    }
                    continue;
                }

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
