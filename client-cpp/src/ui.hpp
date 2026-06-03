#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <limits>
#include "../include/Conversation.hpp"
#include "../include/DecryptedMessage.hpp"

inline void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// ── Colour palette (mirrors web client CSS variables) ─────────────────────────
// Primary pink  #c2185b  → 256-colour 161
// Muted mauve   #660637  → 256-colour 133
// Sent bubble   light    → 256-colour 218
// Danger red    #b91c1c  → 256-colour 160
// White/bright  bold reset
#define C_PRIMARY  "\033[1;38;5;161m"   // bold deep pink  — headings, borders
#define C_MUTED    "\033[38;2;235;164;196m"    // muted mauve     — timestamps, labels
#define C_SENT     "\033[38;5;218m"     // light pink      — your messages
#define C_RECV     "\033[38;5;183m"     // lavender        — received messages
#define C_DANGER   "\033[1;38;5;160m"  // bold red        — errors
#define C_SUCCESS  "\033[38;5;120m"     // soft green      — success notices
#define C_DIM      "\033[2m"  // dim             — secondary info
#define C_BOLD     "\033[1m"
#define C_RESET    "\033[0m"

inline void pauseForUser() {
    std::cout << C_MUTED "\n  ↩  press enter to continue..." C_RESET;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

enum class AuthChoice   { Register, Login, Eof };
enum class MainChoice   { Send, Conversation, ChangePassword, SwitchUser, Logout, Eof };
enum class ConvChoice   { Reply, MessageAction, Refresh, Back, Eof };

// Returns the terminal display width of a UTF-8 string.
static inline int displayWidth(const std::string& s) {
    int w = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if      (c < 0x80)  { w += 1; i += 1; }
        else if (c < 0xE0)  { w += 1; i += 2; }
        else if (c < 0xF0)  { w += 1; i += 3; }
        else                { w += 2; i += 4; }
    }
    return w;
}

static inline void printDivider() {
    std::cout << C_MUTED "  ──────────────────────────────────────\n" C_RESET;
}

static inline void printTitle(const std::string& title) {
    std::cout << "\n" C_PRIMARY C_BOLD "  " << title << "\n" C_RESET
              << C_MUTED "  ──────────────────────────────\n" C_RESET;
}

static inline void printRow(const std::string& line) {
    std::cout << C_RESET "  " << line << "\n";
}

static inline void printPrompt() {
    std::cout << C_MUTED "\n  ✨ " C_PRIMARY "your choice: " C_RESET;
}

// ── Banner ────────────────────────────────────────────────────────────────────
inline void showBanner()
{
    clearScreen();
    std::cout << C_PRIMARY R"(

  ⠀⡤⣂⣭⣭⣅⠢⡀⠤⢀⣀⣀⠂⠾⡿⠛⢿⣦⢡
⠀⠀⡌⢰⡿⠉⠉⠙⣡⣶⣿⣿⣿⣿⣿⣷⣦⡀⣸⡿⠸
⠀⠀⢃⠸⣿⡤⢀⣾⣿⡿⠛⣿⣿⣿⣿⠛⢻⣿⣄⢶⠁     888       888 888               888    .d8888b.            .d8888b.
⠀⠀⠀⠁⢒⢠⣿⣿⣿⣇⣀⢟⡛⠛⠛⡠⢼⣿⣿⡄⡆     888   o   888 888               888   d88P  Y88b          d88P  Y88b
⠀⠀⠀⠀⠸⢸⣿⣿⣿⣿⢱⡿⣷⡦⢴⠇⡇⣿⣿⠃⠇     888  d8b  888 888               888   Y88b.               Y88b.
⠀⠀⠀⠀⠀⠣⢙⠻⢿⣿⣧⣝⣲⠾⢗⣫⠼⢛⠅⠉⠀     888 d888b 888 88888b.   8888b.  888888 "Y888b.    8888b.   "Y888b.
⠀⠀⠀⠀⠀⡀⠤⣃⡒⣬⣭⣭⣭⡭⠭⢴⣦⠑⠠⣀⠀     888d88888b888 888 "88b     "88b 888       "Y88b.     "88b     "Y88b.
⠀⠀⢀⠄⣪⣴⣿⣿⡷⢸⣿⡟⣵⣾⣿⣷⣮⢣⢻⣶⣭⠒⣂⣔⢂⠀88888P Y88888 888  888 .d888888 888         "888 .d888888       "888
⢀⢊⣴⣾⣿⣿⣿⣿⠇⡿⠟⣃⡛⠿⣿⣿⣿⡏⡆⢿⡟⢸⣿⡤⠆⡄8888P   Y8888 888  888 888  888 Y88b. Y88b  d88P 888  888 Y88b  d88P
⢨⠨⡀⠀⠉⢿⣿⠟⡌⡴⢙⡀⣏⠙⡌⣿⣿⣿⢸⠈⣤⢸⣿⡇⠀⡇888P     Y888 888  888 "Y888888  "Y888 "Y8888P"  "Y888888  "Y8888P"
⠀⠑⠬⠑⣒⠪⢄⠺⡇⣿⠉⠀⠀⠉⡇⡿⠿⣣⢏⣼⣿⣿⣿⣧⠀⡇
⠀⠀⠀⠀⠀⠀⠀⠁⠢⠙⣄⠀⠀⢠⢃⡛⠩⠀⠈⠩⠭⠭⠭⠍⠑⠁

)" C_MUTED;
}

// ── Auth menu ─────────────────────────────────────────────────────────────────
inline AuthChoice showAuthMenu()
{
    showBanner();
    printTitle("Welcome to WhatSaS 🧸");
    printRow("1  🌸  register");
    printRow("2  💖  login");
    printPrompt();

    std::string choice;
    while (true) {
        if (!std::getline(std::cin, choice)) return AuthChoice::Eof;
        if (choice == "1") return AuthChoice::Register;
        if (choice == "2") return AuthChoice::Login;
        std::cout << C_DANGER "  please enter 1 or 2: " C_RESET;
    }
}

// ── Main menu ─────────────────────────────────────────────────────────────────
inline MainChoice showMainMenu(const std::string& username)
{
    showBanner();
    printTitle("hey " + username + " 💌");
    printRow("1  📨  send a message");
    printRow("2  💬  open a conversation");
    printRow("3  🔒  change password");
    printRow("4  🔄  switch user");
    printRow("5  🚪  logout");
    printPrompt();

    std::string action;
    while (true) {
        if (!std::getline(std::cin, action)) return MainChoice::Eof;
        if (action == "1") return MainChoice::Send;
        if (action == "2") return MainChoice::Conversation;
        if (action == "3") return MainChoice::ChangePassword;
        if (action == "4") return MainChoice::SwitchUser;
        if (action == "5") return MainChoice::Logout;
        std::cout << C_DANGER "  please enter 1–5: " C_RESET;
    }
}

// ── Peer prompt ───────────────────────────────────────────────────────────────
inline std::string promptPeer()
{
    std::cout << C_MUTED "\n  👤  message to: " C_RESET;
    std::string peer;
    if (!std::getline(std::cin, peer) || peer.empty()) {
        std::cout << C_DANGER "  username cannot be empty\n" C_RESET;
        return "";
    }
    return peer;
}

// ── Message prompt ────────────────────────────────────────────────────────────
inline std::string promptMessage(const std::string& peerId)
{
    std::cout << C_SENT "\n  💬 " C_PRIMARY "→ " C_RESET << peerId << C_MUTED " ❯ " C_RESET;
    std::string text;
    if (!std::getline(std::cin, text) || text.empty()) {
        std::cout << C_DANGER "  message cannot be empty\n" C_RESET;
        return "";
    }
    std::size_t codePoints = 0;
    for (std::size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if      (c < 0x80)  { ++codePoints; i += 1; }
        else if (c < 0xE0)  { ++codePoints; i += 2; }
        else if (c < 0xF0)  { ++codePoints; i += 3; }
        else                { ++codePoints; i += 4; }
    }
    if (codePoints > 2000) {
        std::cout << C_DANGER "  message too long — max 2,000 characters\n" C_RESET;
        return "";
    }
    return text;
}

inline void showError(const std::string& msg) {
    std::cerr << C_DANGER "\n  ✗  " << msg << "\n" C_RESET;
}
inline void showWarning(const std::string& msg) {
    std::cout << C_MUTED "\n  ⚠  " << msg << "\n" C_RESET;
}
inline void showSuccess(const std::string& msg) {
    std::cout << C_SUCCESS "\n  ✓  " << msg << "\n" C_RESET;
}
inline void showInfo(const std::string& msg) {
    std::cout << C_PRIMARY "\n  " << msg << "\n" C_RESET;
}
inline void showRetry() {
    std::cout << C_MUTED "  ↩  try again\n" C_RESET;
}
inline void showFingerprint(const std::string& label, const std::string& fp) {
    std::cout << C_MUTED "  🔑 " << label << ": " C_PRIMARY << fp << C_RESET "\n";
}
inline void showUserNotFound(const std::string& peerId) {
    std::cout << C_DANGER "\n  ✗  could not find '" << peerId
              << "' — user not found or key substitution detected\n" C_RESET;
}

// ── Message action menu ───────────────────────────────────────────────────────
// Returns the action map: action letter → number shown to user.
// isMine=true:    1 delete  2 revoke  3 forward  4 cancel
// isMine=false:   1 forward  2 download  3 cancel
inline void showMessageActionMenu(int idx, const std::string& plaintext, bool isMine)
{
    printTitle("message #" + std::to_string(idx));
    std::cout << C_DIM "  " << plaintext << "\n\n" C_RESET;
    if (isMine) {
        printRow("1  🗑️   delete");
        printRow("2  🔇  revoke");
        printRow("3  ↗️   forward");
        printRow("4  ✖️   cancel");
    } else {
        printRow("1  ↗️   forward");
        printRow("2  💾  download");
        printRow("3  ✖️   cancel");
    }
    std::cout << C_MUTED "\n  action: " C_RESET;
}

inline void showSendResult(bool ok, int statusCode, const std::string& detail)
{
    if (ok) {
        std::cout << C_SUCCESS "\n  ✓  message sent!\n" C_RESET;
    } else {
        std::cout << C_DANGER "\n  ✗  send failed (HTTP "
                  << statusCode << "): " << detail << "\n" C_RESET;
    }
}

// ── Conversation screen ───────────────────────────────────────────────────────
inline void showConversation(const Conversation& conv, const std::string& myId,
                             int newCount,
                             const std::string& myFingerprint,
                             const std::string& peerFingerprint)
{
    const auto msgs = conv.getMessages();
    const std::string& peerId = conv.getPeerId();

    // ── header ───────────────────────────────────────────────────────────────
    std::cout << "\n"
              << C_PRIMARY " 🧸 " C_BOLD << peerId << C_RESET "\n";
    printDivider();
    std::cout << C_MUTED
              << "  🔑 you  : " << myFingerprint << "\n"
              << "  🔑 them : " << peerFingerprint << "\n"
              << "  verify these match out of band\n" C_RESET;

    printDivider();

    if (newCount > 0)
        std::cout << C_SUCCESS "  ✨  " << newCount << " new message(s)\n\n" C_RESET;

    // ── messages ─────────────────────────────────────────────────────────────
    if (msgs.empty()) {
        std::cout << C_MUTED "  (no messages yet — say hi! 💌)\n" C_RESET;
    } else {
        int idx = 1;
        for (const auto& dm : msgs) {
            char timebuf[16] = "??:??";
            std::tm* tm_info = std::localtime(&dm.timestamp);
            if (tm_info) std::strftime(timebuf, sizeof(timebuf), "%d %b %H:%M", tm_info);

            const bool mine = (dm.senderId == myId);
            const bool isPlaceholder = (dm.plaintext == "[message sent]" || dm.plaintext == "[revoked]");

            if (mine) {
                // Sent: number + time on left, "you" label flush right
                std::cout << C_MUTED "  #" << idx++ << C_DIM "  " << timebuf << C_RESET "\n";
                if (isPlaceholder) {
                    std::cout << C_DIM "  ╰─ you: " << dm.plaintext << "\n\n" C_RESET;
                } else {
                    std::cout << C_SENT "  ╰─ you: " C_RESET << dm.plaintext << "\n\n";
                }
            } else {
                std::cout << C_MUTED "  #" << idx++ << C_DIM "  " << timebuf << C_RESET "\n";
                std::cout << C_RECV "  ╰─ " << dm.senderId << ": " C_RESET << dm.plaintext << "\n\n";
            }
        }
    }

    printDivider();
}

// ── Conversation menu ─────────────────────────────────────────────────────────
inline ConvChoice showConversationMenu()
{
    printTitle("what would you like to do?");
    printRow("1  📨  reply");
    printRow("2  📃  message actions");
    printRow("3  🔄  refresh");
    printRow("4  ⬅️  back");
    printPrompt();

    std::string c;
    while (true) {
        if (!std::getline(std::cin, c)) return ConvChoice::Eof;
        if (c == "1") return ConvChoice::Reply;
        if (c == "2") return ConvChoice::MessageAction;
        if (c == "3") return ConvChoice::Refresh;
        if (c == "4") return ConvChoice::Back;
        std::cout << C_DANGER "  enter 1, 2, 3, or 4: " C_RESET;
    }
}

// ── Goodbye ───────────────────────────────────────────────────────────────────
inline void showGoodbye()
{

    std::cout << C_MUTED "💖  bye bye! stay cute ✨\n\n" C_RESET;
}

// Colour macros intentionally left defined so main.cpp can use them.
