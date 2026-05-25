#include <iostream>
#include <string>
#include "AuthCLI.hpp"
#include "../include/Auth.hpp"
#include "../include/HttpClient.hpp"

static const std::string BASE_URL = "https://localhost:5000";

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
        std::getline(std::cin, choice);
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
    try {
        if (choice == "1") {
            auth = Auth::registerUser(http, BASE_URL, creds.username, creds.password);
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
