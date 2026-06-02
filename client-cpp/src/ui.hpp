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
#define C_MUTED    "\033[38;5;161m"    // muted mauve     — timestamps, labels
#define C_SENT     "\033[38;5;218m"     // light pink      — your messages
#define C_RECV     "\033[38;5;183m"     // lavender        — received messages
#define C_DANGER   "\033[1;38;5;160m"  // bold red        — errors
#define C_SUCCESS  "\033[38;5;120m"     // soft green      — success notices
#define C_DIM      "\033[2m"                // dim             — secondary info
#define C_BOLD     "\033[1m"
#define C_RESET    "\033[0m"

inline void pauseForUser() {
    std::cout << C_MUTED "\n                ↩  press enter to continue..." C_RESET;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

enum class AuthChoice   { Register, Login, Eof };
enum class MainChoice   { Send, Conversation, ChangePassword, SwitchUser, Logout, Eof };
enum class ConvChoice   { Reply, MessageAction, Refresh, Back, Eof };

// ── Shared box-drawing helpers ────────────────────────────────────────────────
static inline void printDivider() {
    std::cout << C_MUTED "                ────────────────────────────────────────────────────────\n" C_RESET;
}

static inline void printBoxTop(const std::string& title = "") {
    if (title.empty()) {
        std::cout << C_PRIMARY "                ╭─────────────────────────────────────────────╮\n" C_RESET;
    } else {
        int interior = 45;
        int titleLen = 0;
        for (unsigned char c : title) {
            if ((c & 0xC0) == 0x80) continue;
            titleLen += (c >= 0x80) ? 2 : 1;
        }
        int lpad = (interior - titleLen) / 2;
        int rpad = interior - titleLen - lpad;
        std::cout << C_PRIMARY "                ╭─────────────────────────────────────────────╮\n"
                  << "                │" << std::string(lpad, ' ')
                  << C_BOLD << title << C_PRIMARY
                  << std::string(rpad, ' ') << "│\n"
                  << "                ├─────────────────────────────────────────────┤\n" C_RESET;
    }
}

static inline void printBoxBottom() {
    std::cout << C_PRIMARY "                ╰─────────────────────────────────────────────╯\n" C_RESET;
}

static inline void printBoxRow(const std::string& line) {
    int displayLen = 0;
    for (unsigned char c : line) {
        if ((c & 0xC0) == 0x80) continue;
        displayLen += (c >= 0x80) ? 2 : 1;
    }
    int pad = 45 - displayLen;
    if (pad < 0) pad = 0;
    std::cout << C_PRIMARY "                │ " C_RESET << line << std::string(pad, ' ') << C_PRIMARY " │\n" C_RESET;
}

static inline void printPrompt() {
    std::cout << C_MUTED "\n                ✨ " C_PRIMARY "Your choice: " C_RESET;
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

)" C_MUTED
        "                     💌  secure chats  •  cute vibes  💌\n\n"
    C_RESET;
}

// ── Auth menu ─────────────────────────────────────────────────────────────────
inline AuthChoice showAuthMenu()
{
    showBanner();
    printBoxTop("Welcome to Whatsas 🧸");
    printBoxRow("(1)  🌸  register");
    printBoxRow("(2)  💖  login");
    printBoxBottom();
    printPrompt();

    std::string choice;
    while (true) {
        if (!std::getline(std::cin, choice)) return AuthChoice::Eof;
        if (choice == "1") return AuthChoice::Register;
        if (choice == "2") return AuthChoice::Login;
        std::cout << C_DANGER "                please enter 1 or 2: " C_RESET;
    }
}

// ── Main menu ─────────────────────────────────────────────────────────────────
inline MainChoice showMainMenu(const std::string& username)
{
    showBanner();
    const std::string title = "hey " + username + " 💌";
    printBoxTop(title);
    printBoxRow("(1)   ➤  send a message");
    printBoxRow("(2)  📃  open a conversation");
    printBoxRow("(3)  🔒  change password");
    printBoxRow("(4)  🔄  switch user");
    printBoxRow("(5)  🚪  logout");
    printBoxBottom();
    printPrompt();

    std::string action;
    while (true) {
        if (!std::getline(std::cin, action)) return MainChoice::Eof;
        if (action == "1") return MainChoice::Send;
        if (action == "2") return MainChoice::Conversation;
        if (action == "3") return MainChoice::ChangePassword;
        if (action == "4") return MainChoice::SwitchUser;
        if (action == "5") return MainChoice::Logout;
        std::cout << C_DANGER "                please enter 1–5: " C_RESET;
    }
}

// ── Peer prompt ───────────────────────────────────────────────────────────────
inline std::string promptPeer()
{
    std::cout << C_MUTED "\n                👤  Message to: " C_RESET;
    std::string peer;
    if (!std::getline(std::cin, peer) || peer.empty()) {
        std::cout << C_DANGER "                username cannot be empty\n" C_RESET;
        return "";
    }
    return peer;
}

// ── Message prompt ────────────────────────────────────────────────────────────
inline std::string promptMessage(const std::string& peerId)
{
    std::cout << C_SENT "\n                💬  " C_PRIMARY "→ " C_RESET << peerId << C_MUTED "  ❯  " C_RESET;
    std::string text;
    if (!std::getline(std::cin, text) || text.empty()) {
        std::cout << C_DANGER "                message cannot be empty\n" C_RESET;
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
        std::cout << C_DANGER "                message too long — max 2,000 characters\n" C_RESET;
        return "";
    }
    return text;
}

inline void showSendResult(bool ok, int statusCode, const std::string& detail)
{
    if (ok) {
        std::cout << C_SUCCESS "\n                ✓  message sent!\n" C_RESET;
    } else {
        std::cout << C_DANGER "\n                ✗  send failed (HTTP "
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
              << C_PRIMARY "                💬  " C_BOLD << peerId << C_RESET "\n";
    printDivider();
    std::cout << C_MUTED
              << "                🔑 you  : " << myFingerprint << "\n"
              << "                🔑 them : " << peerFingerprint << "\n"
              << "                verify these match out of band\n" C_RESET;

    printDivider();

    if (newCount > 0)
        std::cout << C_SUCCESS "                ✨  " << newCount << " new message(s)\n\n" C_RESET;

    // ── messages ─────────────────────────────────────────────────────────────
    if (msgs.empty()) {
        std::cout << C_MUTED "                (no messages yet — say hi! 💌)\n" C_RESET;
    } else {
        int idx = 1;
        for (const auto& dm : msgs) {
            char timebuf[18] = "??:??";
            std::tm* tm_info = std::localtime(&dm.timestamp);
            if (tm_info) std::strftime(timebuf, sizeof(timebuf), "%d %b %H:%M", tm_info);

            const bool mine = (dm.senderId == myId);

            const bool isPlaceholder = (dm.plaintext == "[message sent]" || dm.plaintext == "[revoked]");

            if (mine) {
                std::cout << C_MUTED "                #" << idx++ << "  " << timebuf << "  → you\n" C_RESET;
                if (isPlaceholder)
                    std::cout << C_DIM "                " << dm.plaintext << "\n\n" C_RESET;
                else
                    std::cout << C_SENT "                " << dm.plaintext << "\n\n" C_RESET;
            } else {
                std::cout << C_MUTED "                #" << idx++ << "  " << timebuf << "  ← " << dm.senderId << "\n" C_RESET;
                std::cout << C_RECV  "                " << dm.plaintext << "\n\n" C_RESET;
            }
        }
    }

    printDivider();
}

// ── Conversation menu ─────────────────────────────────────────────────────────
inline ConvChoice showConversationMenu()
{
    std::cout << C_MUTED
              << "                what would you like to do?\n\n"
              << "                " C_PRIMARY "(1)" C_MUTED "   ➤  type a reply\n"
              << "                " C_PRIMARY "(2)" C_MUTED "  ✨  message actions — type a message number (e.g. #3) to select it\n"
              << "                " C_PRIMARY "(3)" C_MUTED "  🔄  refresh — fetch new messages\n"
              << "                " C_PRIMARY "(4)" C_MUTED "  ⬅️   go back to the main menu\n\n"
              << C_RESET;
    printPrompt();

    std::string c;
    while (true) {
        if (!std::getline(std::cin, c)) return ConvChoice::Eof;
        if (c == "1") return ConvChoice::Reply;
        if (c == "2") return ConvChoice::MessageAction;
        if (c == "3") return ConvChoice::Refresh;
        if (c == "4") return ConvChoice::Back;
        std::cout << C_DANGER "                enter 1, 2, 3, or 4: " C_RESET;
    }
}

// ── Goodbye ───────────────────────────────────────────────────────────────────
inline void showGoodbye()
{

    std::cout << C_MUTED "💖  bye bye! stay cute ✨\n\n" C_RESET;
}

// Colour macros intentionally left defined so main.cpp can use them.
