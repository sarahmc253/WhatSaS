# WhatSaS C++ Client

Secure messaging client built in C++17 using libsodium (AES-256-GCM + X25519), libcurl (HTTPS), and SQLite3 (local message store).

## Prerequisites

This project builds with CMake on Windows via MSYS2 (ucrt64 toolchain).

### 1. Install MSYS2

Download and install from https://www.msys2.org/. Open the **UCRT64** shell.

### 2. Install dependencies

```sh
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-curl \
          mingw-w64-ucrt-x86_64-libsodium \
          mingw-w64-ucrt-x86_64-sqlite3
```

## Build

From the `client-cpp` directory:

```sh
cmake -B build
cmake --build build
```

## Run tests

```powershell
.\build\sas-tests.exe
```

Expected output: all tests pass with no failures.

## Run application

```powershell
.\build\sas-client.exe
```

## Project structure

```
client-cpp/
  include/
    User.hpp          — user identity and Curve25519 public key
    Message.hpp       — encrypted message payload (ciphertext, nonce, IDs, timestamp)
    MessageStore.hpp  — in-memory store: std::vector (primary) + std::map (recipient index)
    Conversation.hpp  — conversation state between two users
    Client.hpp        — network layer (libcurl HTTPS + libsodium E2EE)
  src/
    User.cpp
    Message.cpp
    MessageStore.cpp
    Conversation.cpp
    Client.cpp
    main.cpp
  tests/
    test_helpers.hpp  — CHECK_EQ and CHECK_TRUE macros
    test_message.cpp  — Message class tests
    test_user.cpp     — User class tests
    test_store.cpp    — MessageStore class tests
    test_main.cpp     — test runner entry point
  CMakeLists.txt
  README.md
```

## Cryptography

| Primitive | Algorithm | Library |
|---|---|---|
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium |
| Key exchange | HPKE Mode\_Auth, DHKEM(X25519, HKDF-SHA256) — RFC 9180 | libsodium |
| Nonce | 12-byte CSPRNG | libsodium `randombytes_buf` |
| Transport | HTTPS/TLS | libcurl |
