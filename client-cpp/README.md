# WhatSaS C++ Client

Secure messaging client in C++17. Uses raw BSD sockets (Winsock2) + OpenSSL for HTTPS, libsodium for E2EE encryption (AES-256-GCM), and SQLite3 for local message storage.

## Prerequisites

MSYS2 with the ucrt64 toolchain. Open the **UCRT64** shell and install:

```sh
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-libsodium \
          mingw-w64-ucrt-x86_64-sqlite3
```

## Build

```powershell
cmake -B build
cmake --build build
```

## Run

```powershell
.\build\sas-client.exe
```

## Tests

```powershell
# Offline TCP tests (no internet required)
.\build\test_tcp.exe

# HTTPS/TLS tests (internet required)
.\build\test_http.exe --network

# CTest — offline only by default
ctest --test-dir build

# CTest — include network tests
ctest --test-dir build -L network
```

## Project structure

```
client-cpp/
  include/                      — public headers
    User.hpp                    — user identity + Curve25519 public key
    Message.hpp                 — immutable encrypted message (ciphertext, nonce, IDs, timestamp)
    MessageStore.hpp            — dual-index store: std::vector + std::map
    HttpClient.hpp              — HTTPS GET client (raw sockets + OpenSSL TLS)
    Conversation.hpp            — conversation state (scaffold)
    Client.hpp                  — high-level network + encryption layer (scaffold)
  src/                          — implementation
    User.cpp
    Message.cpp
    MessageStore.cpp
    HttpClient.cpp              — orchestration: connects TCP, TLS, HTTP modules
    tcp_connect.hpp             — internal: raw TCP connect with 5s timeout
    tls_connect.hpp / .cpp      — internal: TLS handshake, Windows CA store, cert validation
    http_response.hpp / .cpp    — internal: URL parser, HTTP framing, chunked decoder
    Conversation.cpp            — scaffold
    Client.cpp                  — scaffold
    main.cpp
  tests/
    test_helpers.hpp            — CHECK_EQ / CHECK_TRUE macros
    test_main.cpp               — test runner
    test_message.cpp            — Message class tests
    test_user.cpp               — User class tests
    test_store.cpp              — MessageStore tests
    test_tcp.cpp                — raw TCP socket tests (offline)
    test_http.cpp               — HTTPS GET tests (--network)
  CMakeLists.txt
  README.md
```

## Cryptography

| Primitive | Algorithm | Library |
|-----------|-----------|---------|
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium `crypto_aead_aes256gcm_*` |
| Key exchange | X25519 ECDH + HPKE RFC 9180 | libsodium `crypto_scalarmult_*`, `crypto_box_*` |
| Key derivation | HKDF-SHA256 | libsodium `crypto_kdf_hkdf_sha256_*` |
| Password hashing | Argon2id | libsodium `crypto_pwhash_*` |
| Random nonces | 12-byte CSPRNG | libsodium `randombytes_buf` |
| Transport | HTTPS/TLS 1.2+ | Raw OpenSSL (libssl + libcrypto) |
| Cert validation | System CA store | Windows `CertOpenSystemStoreA` + OpenSSL X509 |

**Notes:**
- HPKE RFC 9180 (`DHKEM(X25519, HKDF-SHA256)`) requires manual implementation — libsodium provides the primitives but not HPKE directly
- TLS certificate chain is validated against the Windows system root CA store
- Hostname verification enforced via `SSL_set1_host` — connections are rejected on CN/SAN mismatch
- Plain HTTP is rejected — this client is HTTPS only

## Manual TLS verification

Cross-check the client's cert validation against the reference tool:

```sh
# Valid cert — expect: Verify return code: 0 (ok)
openssl s_client -connect sas.theburkenator.com:443 -verify 5

# Expired cert — expect: Verify return code: 10 (certificate has expired)
openssl s_client -connect expired.badssl.com:443 -verify 5

# Hostname mismatch — expect: Verify return code: 62 (hostname mismatch)
openssl s_client -connect wrong.host.badssl.com:443 -verify_hostname wrong.host.badssl.com
```
