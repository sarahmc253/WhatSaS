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

From the `client-cpp` directory in PowerShell or the MSYS2 UCRT64 shell:

```sh
cmake -B build
cmake --build build
```

## Run

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
