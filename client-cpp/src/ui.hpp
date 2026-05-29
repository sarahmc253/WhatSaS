#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include "../include/Conversation.hpp"
#include "../include/DecryptedMessage.hpp"

#define M "\033[1;35m"
#define R "\033[0m"
#define RED "\033[1;31m"

// ── banner ────────────────────────────────────────────────────────────────────
inline void showBanner()
{
    std::cout << M << R"(

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

                    🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀🎀

)" << R;
}

// ── auth menu ─────────────────────────────────────────────────────────────────
inline std::string showAuthMenu()
{
    std::cout << M << R"(
        ┌──────────────────────────────────────────┐
        │                                          │
        │   💌  welcome to whatsas!                │
        │                                          │
        │      [1]  🌸  register                   │
        │      [2]  💖  login                      │
        │                                          │
        └──────────────────────────────────────────┘

)" << "        ✨ your choice: " << R;

    std::string choice;
    while (true) {
        if (!std::getline(std::cin, choice)) return "";
        if (choice == "1" || choice == "2") return choice;
        std::cout << M "        💔 please enter 1 or 2: " R;
    }
}

// ── main menu ─────────────────────────────────────────────────────────────────
inline std::string showMainMenu(const std::string& username)
{
    std::cout << M << "\n"
        "        ┌──────────────────────────────────────────┐\n"
        "        │                                          │\n"
        "        │   💌  hey " << username;

    const int pad = 27 - static_cast<int>(username.size());
    for (int i = 0; i < pad; ++i) std::cout << ' ';

    std::cout <<
        "│\n"
        "        │                                          │\n"
        "        │      [1]  📨  send a message             │\n"
        "        │      [2]  💬  open a conversation        │\n"
        "        │      [3]  🚪  logout                     │\n"
        "        │                                          │\n"
        "        └──────────────────────────────────────────┘\n"
        "\n        ✨ your choice: " R;

    std::string action;
    while (true) {
        if (!std::getline(std::cin, action)) return "";
        if (action == "1" || action == "2" || action == "3") return action;
        std::cout << M "        💔 please enter 1, 2, or 3: " R;
    }
}

// ── peer prompt ───────────────────────────────────────────────────────────────
inline std::string promptPeer()
{
    std::cout << M "\n        👤 who do you want to message? " R;
    std::string peer;
    if (!std::getline(std::cin, peer) || peer.empty()) {
        std::cout << M "        💔 username cannot be empty\n" R;
        return "";
    }
    return peer;
}

// ── message prompt ────────────────────────────────────────────────────────────
inline std::string promptMessage(const std::string& peerId)
{
    std::cout << M "        💬 message to " << peerId << ": " R;
    std::string text;
    if (!std::getline(std::cin, text) || text.empty()) {
        std::cout << M "        💔 message cannot be empty\n" R;
        return "";
    }
    return text;
}

inline void showSendResult(bool ok, int statusCode, const std::string& detail)
{
    if (ok) {
        std::cout << M "\n        💌 message sent! ✅\n" R;
    } else {
        std::cout << RED "\n        💔 send failed (HTTP "
                  << statusCode << "): " << detail << "\n" R;
    }
}

// ── conversation screen ───────────────────────────────────────────────────────
inline void showConversation(const Conversation& conv, const std::string& myId, int newCount)
{
    const auto msgs = conv.getMessages();
    const std::string& peerId = conv.getPeerId();

    std::cout << "\n" M
        "        ╔══════════════════════════════════════════╗\n"
        "        ║  💬  conversation with " << peerId;

    const int pad = 19 - static_cast<int>(peerId.size());
    for (int i = 0; i < pad; ++i) std::cout << ' ';
    std::cout <<
        "║\n"
        "        ╚══════════════════════════════════════════╝\n" R;

    if (msgs.empty()) {
        std::cout << M "        (no messages yet)\n" R;
    } else {
        for (const auto& dm : msgs) {
            char timebuf[6] = "??:??";
            std::tm* tm_info = std::localtime(&dm.timestamp);
            if (tm_info) std::strftime(timebuf, sizeof(timebuf), "%H:%M", tm_info);

            const bool mine = (dm.senderId == myId);
            if (mine) {
                std::cout << M "        [" << timebuf << "] you: " R << dm.plaintext << "\n";
            } else {
                std::cout << M "        [" << timebuf << "] " << dm.senderId << ": " R << dm.plaintext << "\n";
            }
        }
    }

    std::cout << M "\n        (" << newCount << " new message(s) received)\n" R;
}

// ── conversation action menu ──────────────────────────────────────────────────
inline std::string showConversationMenu()
{
    std::cout << M "\n"
        "        ┌──────────────────────────────────────────┐\n"
        "        │      [1]  📨  reply                      │\n"
        "        │      [2]  ↩   back to main menu          │\n"
        "        └──────────────────────────────────────────┘\n"
        "\n        ✨ your choice: " R;

    std::string c;
    while (true) {
        if (!std::getline(std::cin, c)) return "2";
        if (c == "1" || c == "2") return c;
        std::cout << M "        💔 please enter 1 or 2: " R;
    }
}

// ── goodbye ───────────────────────────────────────────────────────────────────
inline void showGoodbye()
{
    std::cout << M "\n        💖 bye bye! stay cute ✨\n\n" R;
}

#undef M
#undef R
#undef RED
