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

inline Credentials promptCredentials() {
    std::string username;
    std::cout << "Username: ";
    std::getline(std::cin, username);

    std::string password;
    std::cout << "Password: ";

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
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

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

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif

    std::cout << '\n';
    return { username, password };
}
