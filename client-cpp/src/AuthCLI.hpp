#pragma once

#include <iostream>
#include <string>

#ifdef _WIN32
#  include <conio.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

struct Credentials {
    std::string username;
    std::string password;
};

struct LoginCredentials {
    std::string username;
    std::string password;
};

static std::string readPassword(const char* label = "Password: ") {
    std::string password;
    std::cout << label;

#ifdef _WIN32
    int ch;
    while ((ch = _getch()) != '\r') {
        if (ch == 0 || ch == 0xE0) {
            _getch(); // consume second byte of a special-key sequence (arrows, Fn keys)
            continue;
        }
        if (ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (ch >= 32 && ch < 127) {
            password.push_back(static_cast<char>(ch));
            std::cout << '*' << std::flush;
        }
    }
#else
    termios oldt{};
    termios newt{};
    bool termiosApplied = false;
    if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
        newt = oldt;
        newt.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);
        newt.c_cc[VMIN]  = 1;
        newt.c_cc[VTIME] = 0;
        termiosApplied = (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0);
    }

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (ch == 127 || ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (ch >= 32 && ch < 127) {
            password.push_back(static_cast<char>(ch));
            std::cout << '*' << std::flush;
        }
    }

    if (termiosApplied) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif

    std::cout << '\n';
    return password;
}

inline LoginCredentials promptLogin() {
    std::string username;
    std::cout << "Username: ";
    std::getline(std::cin, username);
    return { username, readPassword() };
}

// Prompts for old password then new password (with confirmation loop).
// Returns {oldPassword, newPassword} or {"",""} if stdin closes.
struct PasswordChange { std::string oldPassword; std::string newPassword; };
inline PasswordChange promptPasswordChange() {
    std::string oldPw = readPassword("Old password: ");
    if (oldPw.empty()) return {};

    while (true) {
        std::string newPw   = readPassword("New password: ");
        std::string confirm = readPassword("Confirm new:  ");
        if (newPw == confirm) return { oldPw, newPw };
        std::cout << "\033[1;31m        💔 passwords don't match — try again\n\033[0m";
    }
}

inline Credentials promptCredentials() {
    std::string username;
    std::cout << "Username: ";
    std::getline(std::cin, username);

    return { username, readPassword() };
}
