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

```text
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
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium via `crypto_aead_aes256gcm_*` |
| Key derivation | HKDF-SHA256 | libsodium via `crypto_kdf_hkdf_sha256_*` |
| Authenticated encryption | AEAD with associated data (sender_id + recipient_id + timestamp) | libsodium |
| Key exchange | X25519 Elliptic Curve Diffie-Hellman | libsodium via `crypto_box_*` and `crypto_scalarmult_*` |
| Random nonce | 12-byte CSPRNG (AES-256-GCM requirement) | libsodium `randombytes_buf` |
| Transport | HTTPS/TLS | libcurl with mandatory certificate validation |

**Notes on libsodium:**
- libsodium provides `crypto_aead_aes256gcm_*` for AES-256-GCM encryption/decryption
- X25519 key exchange is available via `crypto_box_*` (public key cryptography) and `crypto_scalarmult_*` (raw scalar multiplication)
- For HPKE RFC 9180 mode, manual implementation of HPKE is required; libsodium does not provide HPKE directly
- All cryptographic functions use constant-time operations to prevent timing attacks
