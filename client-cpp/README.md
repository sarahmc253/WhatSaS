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
# Offline TCP socket tests (no internet required)
.\build\test_tcp.exe

# Offline crypto unit tests (no internet required)
.\build\test_client.exe

# HTTPS/TLS tests (internet required)
.\build\test_http.exe --network

# CTest — offline only by default
ctest --test-dir build

# CTest — include network tests
ctest --test-dir build -L network
```

## Project structure

```text
client-cpp/
  include/                      — public headers
    User.hpp                    — user identity + Curve25519 public key
    Message.hpp                 — immutable encrypted message (ciphertext, nonce, IDs, timestamp)
    MessageStore.hpp            — dual-index store: std::vector + std::map
    HttpClient.hpp              — HTTPS GET + POST client (raw sockets + OpenSSL TLS)
    Client.hpp                  — high-level encryption + network layer (AES-256-GCM)
    Conversation.hpp            — conversation state (scaffold)
  src/                          — implementation
    User.cpp
    Message.cpp
    MessageStore.cpp
    HttpClient.cpp              — orchestration: doRequest(), get(), post()
    Client.cpp                  — sendMessage(), getMessages()
    tcp_connect.hpp             — internal: raw TCP connect with 5s timeout
    tls_connect.hpp / .cpp      — internal: TLS handshake, Windows CA store, cert validation
    http_response.hpp / .cpp    — internal: URL parser, GET/POST HTTP framing, chunked decoder
    crypto_utils.hpp            — internal: jsonEscape, b64Encode, generateMsgId, buildAd
    Conversation.cpp            — scaffold
    main.cpp
  tests/
    test_helpers.hpp            — CHECK_EQ / CHECK_TRUE macros
    test_main.cpp               — test runner
    test_message.cpp            — Message class tests
    test_user.cpp               — User class tests
    test_store.cpp              — MessageStore tests
    test_tcp.cpp                — raw TCP socket tests (offline)
    test_http.cpp               — HTTPS GET tests (--network)
    test_client.cpp             — AES-256-GCM crypto tests (offline)
  CMakeLists.txt
  README.md
```

## Cryptography

| Primitive | Algorithm | Library |
|-----------|-----------|---------|
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium `crypto_aead_aes256gcm_*` |
| Nonce generation | Counter-XOR-base (96-bit, unique per key) | libsodium `randombytes_buf` + `std::atomic` |
| Associated data | Canonical JSON, bound into AEAD tag | Manual (no spaces, fixed key order) |
| Message ID | 16 CSPRNG bytes → 32 hex chars | libsodium `randombytes_buf` + `sodium_bin2hex` |
| Base64 encoding | RFC 4648 standard | libsodium `sodium_bin2base64` |
| Key exchange | X25519 ECDH + HPKE RFC 9180 (pending Day 7) | libsodium `crypto_scalarmult_*` |
| Key derivation | HKDF-SHA256 (pending) | libsodium `crypto_kdf_hkdf_sha256_*` |
| Password hashing | Argon2id (pending) | libsodium `crypto_pwhash_*` |
| Transport | HTTPS/TLS 1.2+ | Raw OpenSSL (libssl + libcrypto) |
| Cert validation | System CA store | Windows `CertOpenSystemStoreA` + OpenSSL X509 |

**Notes:**
- AES-256-GCM requires hardware AES-NI (Intel/AMD); `Client` constructor throws `std::runtime_error` if unavailable
- Nonce scheme: `nonceBase_` (CSPRNG, set once at construction) XOR'd with an atomic counter — unique for 2^64 messages per key, eliminating birthday collision risk of purely random nonces
- Associated data (sender_id, recipient_id, message_id, timestamp) is bound into the AEAD tag but not encrypted — any tampering causes decryption to fail
- HPKE RFC 9180 (`DHKEM(X25519, HKDF-SHA256)`) is a Day 7 task; `kem_output` and `sender_ephemeral_pk` fields in POST JSON are empty placeholders until then
- TLS certificate chain validated against the Windows system root CA store
- Hostname verification enforced via `SSL_set1_host` — connections rejected on CN/SAN mismatch
- Plain HTTP rejected — this client is HTTPS only

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
