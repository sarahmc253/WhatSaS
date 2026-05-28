#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include "AuthCLI.hpp"
#include "key_wrap.hpp"
#include "../include/Auth.hpp"
#include "../include/HttpClient.hpp"

static const std::string BASE_URL = "https://sas.theburkenator.com";

// Returns the absolute path to certs/server.crt relative to the exe's own directory.
// Independent of the working directory the exe was launched from.
static std::string certPath() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return "";
    std::wstring exeDir(buf, len);
    auto slash = exeDir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return "";
    exeDir = exeDir.substr(0, slash + 1);  // keep trailing separator
    // Convert to narrow UTF-8
    int nb = WideCharToMultiByte(CP_UTF8, 0, exeDir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (nb <= 0) return "";
    std::string dir(nb - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, exeDir.c_str(), -1, &dir[0], nb, nullptr, nullptr);
    return dir + "certs\\server.crt";
}

int main() {

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

    HttpClient http(certPath());
    Auth auth;
    try {
        if (choice == "1") {
            const auto kp      = generateX25519Keypair();
            const auto wrapped = wrapPrivateKey(kp, creds.password);
            auth = Auth::registerUser(http, BASE_URL,
                                      creds.username, creds.password,
                                      creds.email,
                                      wrapped.x25519PublicKeyB64,
                                      wrapped.wrappedPrivateKeyB64,
                                      wrapped.kekSaltB64);
            std::cout << "\033[1;35m\n        🌸 registered! welcome to whatsas, " << creds.username << "~ 💖\n";
        } else {
            auth = Auth::login(http, BASE_URL, creds.username, creds.password);
            std::cout << "\033[1;35m\n        💖 logged in! welcome back, " << creds.username << "~ 🎀\n";
        }

        std::cout << "        🔑 session token received ✅\n\033[0m\n";

    } catch (const std::runtime_error& e) {
        std::cerr << "\033[1;31m\n        💔 " << e.what() << "\n\033[0m\n";
        return 1;
    }

    // auth and http are both live here — pass auth.getToken() as the authToken
    // argument to any subsequent http.post() calls, e.g.:
    //   http.post(BASE_URL + "/api/messages", body, "application/json", auth.getToken())

    return 0;
}
