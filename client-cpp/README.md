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
          mingw-w64-ucrt-x86_64-sqlite3 \
          mingw-w64-ucrt-x86_64-nlohmann-json
```

## Build

```powershell
cmake -B build
cmake --build build
```

## Server certificate

The server uses a self-signed TLS certificate. Place it at `client-cpp/certs/server.crt` before running.

> **Note:** `certs/` is gitignored — do not commit the cert file.

### Team members (access to the VM)

SSH into the VM and copy the cert directly:

```bash
scp -P 2200 student@sas.theburkenator.com:/path/to/server.crt client-cpp/certs/server.crt
```

Or copy it from within the VM session:

```bash
ssh student@sas.theburkenator.com -p 2200
# then copy the cert to your local machine via scp or paste the contents
```

### External / general setup

Obtain `server.crt` from a team member and place it in the `certs/` directory:

```powershell
mkdir certs
copy path\to\server.crt certs\server.crt
```

The client loads this cert at startup (`loadPinnedCert` in `tls_connect.cpp`) and adds it to the OpenSSL trust store so the TLS handshake with `sas.theburkenator.com` succeeds even though the cert is not signed by a public CA.

## Run

```powershell
# Must be run from the client-cpp\ directory so certs/server.crt resolves correctly
.\build\sas-client.exe
```

## Tests

```powershell
# Full E2E chain: register, login, send, receive — no network
.\build\test_e2e.exe

# DHKEM(X25519) + HKDF-SHA256 key derivation
.\build\test_hpke.exe

# AES-256-GCM tamper detection
.\build\test_tamper.exe

# Client crypto unit tests
.\build\test_client.exe

# Conversation deduplication and ordering
.\build\test_conversation.exe

# Raw TCP socket tests (no internet required)
.\build\test_tcp.exe

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
    DecryptedMessage.hpp        — plain struct: messageId, senderId, recipientId, plaintext, timestamp
    Conversation.hpp            — decrypted message organiser for a single peer; printConversation()
  src/                          — implementation
    User.cpp
    Message.cpp
    MessageStore.cpp
    HttpClient.cpp              — orchestration: doRequest(), get(), post()
    Client.cpp                  — sendMessage(), getMessages(), receiveMessages()
    message_crypto.hpp          — internal: encryptMessage(), decryptMessage() (message-level AEAD)
    tcp_connect.hpp             — internal: raw TCP connect with 5s timeout
    tls_connect.hpp / .cpp      — internal: TLS handshake, Windows CA store, cert validation
    http_response.hpp / .cpp    — internal: URL parser, GET/POST HTTP framing, chunked decoder
    crypto_utils.hpp            — internal: b64Encode, b64Decode, generateMsgId, buildAd (ordered_json)
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
    test_conversation.cpp       — Conversation + message_crypto offline tests
  CMakeLists.txt
  README.md
```

## Cryptography

| Primitive | Algorithm | Library |
|-----------|-----------|---------|
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium `crypto_aead_aes256gcm_*` |
| Nonce generation | Fresh CSPRNG per message (96-bit) | libsodium `randombytes_buf` |
| Associated data | Canonical JSON, bound into AEAD tag | nlohmann `ordered_json` (guaranteed key insertion order) |
| Message ID | 16 CSPRNG bytes → 32 hex chars | libsodium `randombytes_buf` + `sodium_bin2hex` |
| Base64 encoding | RFC 4648 standard | libsodium `sodium_bin2base64` |
| Key exchange | DHKEM(X25519) — ephemeral + static DH, authenticated sender | libsodium `crypto_scalarmult_*` |
| Key derivation | HKDF-SHA256 over DH1 \|\| DH2 → 32-byte AES key | libsodium `crypto_kdf_hkdf_sha256_*` |
| Password hashing / KEK derivation | Argon2id, INTERACTIVE ops/mem, 16-byte random salt | libsodium `crypto_pwhash_*` |
| Private key wrapping | AES-256-GCM over X25519 sk; envelope = nonce \|\| ct \|\| tag | libsodium `crypto_aead_aes256gcm_*` |
| Transport | HTTPS/TLS 1.2+ | Raw OpenSSL (libssl + libcrypto) |
| Cert validation | System CA store | Windows `CertOpenSystemStoreA` + OpenSSL X509 |

**Notes:**
- AES-256-GCM requires hardware AES-NI (Intel/AMD); `Client` constructor throws `std::runtime_error` if unavailable
- Nonce scheme: fresh 96-bit nonce drawn from `randombytes_buf` on every `encryptAes256Gcm` call — no counter, no state carried between messages
- Associated data (sender_id, recipient_id, message_id, timestamp) is bound into the AEAD tag but not encrypted — any tampering causes decryption to fail
- HPKE-style key establishment is fully implemented: each message derives a fresh AES key via `DH1 = X25519(eph_sk, recipient_pk)` and `DH2 = X25519(sender_sk, recipient_pk)`; `kem_output` carries the ephemeral public key; `sender_ephemeral_pk` is a deprecated wire field kept for server schema compatibility
- Private keys are stored server-side wrapped under a password-derived KEK (Argon2id → AES-256-GCM); the server never holds a plaintext private key
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
