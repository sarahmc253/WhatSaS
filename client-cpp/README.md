# WhatSaS C++ Client

Secure messaging client in C++17. Uses raw BSD sockets (Winsock2) + OpenSSL for HTTPS, and libsodium for E2EE encryption (AES-256-GCM).

> **Platform: Windows only.**
> The networking layer uses Winsock2 (`winsock2.h`), the Windows CA store (`wincrypt.h`, `CertOpenSystemStoreA`), and Windows socket types (`SOCKET`, `INVALID_SOCKET`, `WSAGetLastError`). It will not compile on Linux or macOS without a porting effort.

---

## Prerequisites

You need **MSYS2** with the UCRT64 toolchain. Check if you already have it:

```powershell
where.exe cmake   # should show C:\msys64\ucrt64\bin\cmake.exe
where.exe ninja   # should show C:\msys64\ucrt64\bin\ninja.exe
```

If either is missing, install MSYS2 from https://www.msys2.org, open the **UCRT64** shell (not MinGW64, not MSYS), and run:

```sh
pacman -Syu
```

Then close and reopen the UCRT64 shell and install the dependencies:

```sh
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-libsodium \
          mingw-w64-ucrt-x86_64-nlohmann-json
```

If you already have MSYS2, you can just run the `pacman -S` line — it will skip anything already installed.

Build commands can be run from **PowerShell** or the **UCRT64 shell** — both work as long as MSYS2 is installed.

---

## Server certificate

The server uses a self-signed TLS certificate.

> `certs/` is gitignored — do not commit the cert file.

### SSH access to the VM

```sh
# Run this from local machine in any terminal with scp
scp -P 2200 student@sas.theburkenator.com:/path/to/server.crt client-cpp/certs/server.crt
```

---

## Build

Run from the `client-cpp/` directory in PowerShell:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

A successful build produces `build/sas-client.exe`.

---

## Run

Must be run from the `client-cpp/` directory so `certs/server.crt` resolves correctly.

Open PowerShell, navigate to `client-cpp/`, then:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
.\build\sas-client.exe
```

The UTF-8 line is required for emoji and Unicode to display correctly in the terminal.

---

## Tests

Run from `client-cpp/` in PowerShell. Offline tests only by default:

```powershell
ctest --test-dir build
```

Individual binaries (all offline unless noted):

| Binary | What it tests |
|--------|---------------|
| `.\build\test_e2e.exe` | Full register → login → send → receive chain |
| `.\build\test_hpke.exe` | DHKEM(X25519) + HKDF-SHA256 key derivation |
| `.\build\test_tamper.exe` | AES-256-GCM tamper detection |
| `.\build\test_client.exe` | Client crypto unit tests |
| `.\build\test_conversation.exe` | Conversation deduplication and ordering |
| `.\build\test_tcp.exe` | Raw TCP socket tests |
| `.\build\test_http.exe --network` | HTTPS/TLS tests *(internet required)* |

To include network tests via CTest: `ctest --test-dir build -L network`

---

## Project structure

```text
client-cpp/
  include/              public headers
    Auth.hpp            auth state (token, userId, wrapped key)
    Client.hpp          high-level E2EE send/receive + TOFU key pinning
    Conversation.hpp    decrypted message store for one peer
    DecryptedMessage.hpp plain struct: messageId, senderId, recipientId, plaintext, timestamp
    HttpClient.hpp      HTTPS GET / POST / DELETE (raw sockets + OpenSSL)
    Message.hpp         immutable encrypted message value type
    MessageStore.hpp    insertion-ordered store + unordered_map index by peer key
    User.hpp            user identity + X25519 public key

  src/                  implementation + internal headers
    main.cpp            entry point and main UI loop
    Auth.cpp            login, register, logout, change-password
    Client.cpp          sendMessage(), receiveMessages(), fetchPeerPublicKey()
    HttpClient.cpp      doRequest() pipeline: TCP → TLS → write → read → parse
    tcp_connect.hpp     raw Winsock2 TCP connect with 5 s timeout
    tls_connect.hpp/.cpp  TLS handshake, cert pinning, hostname verification
    http_response.hpp/.cpp  URL parser, request builders, response parser
    hpke_utils.hpp      DHKEM(X25519): hpkeSend() / hpkeReceive()
    message_crypto.hpp  encryptMessage() / decryptMessage() (AES-256-GCM)
    crypto_utils.hpp    b64Encode/Decode, generateMsgId, buildAd, HKDF, keyFingerprint
    key_wrap.hpp        wrapPrivateKey() / unwrapPrivateKey() (Argon2id KEK → AES-GCM)
    AuthCLI.hpp         CLI prompts for auth flows
    ui.hpp              banner, menus, conversation display

  tests/                offline unit + integration tests
    test_e2e.cpp        full crypto chain (no network)
    test_hpke.cpp       DHKEM key derivation
    test_tamper.cpp     AES-GCM tamper detection
    test_client.cpp     crypto unit tests
    test_conversation.cpp  deduplication and ordering
    test_tcp.cpp        TCP socket tests
    test_http.cpp       HTTPS tests (--network flag required)
```

---

## Cryptography

| Primitive | Algorithm | Library |
|-----------|-----------|---------|
| Symmetric encryption | AES-256-GCM (AEAD) | libsodium `crypto_aead_aes256gcm_*` |
| Nonce generation | Fresh CSPRNG per message (96-bit) | libsodium `randombytes_buf` |
| Associated data | Canonical JSON, bound into AEAD tag | nlohmann `ordered_json` (guaranteed key insertion order) |
| Message ID | 16 CSPRNG bytes → 32 hex chars | libsodium `randombytes_buf` + `sodium_bin2hex` |
| Base64 encoding | RFC 4648 standard | libsodium `sodium_bin2base64` |
| Key exchange | DHKEM(X25519) — ephemeral + static DH, authenticated sender | libsodium `crypto_scalarmult_*` |
| Key derivation | HKDF-Extract(salt=eph_pk, IKM=DH1\|\|DH2) → PRK; HKDF-Expand(PRK, info) → 32-byte AES key | libsodium `crypto_auth_hmacsha256_*` (manual RFC 5869) |
| Password hashing / KEK derivation | Argon2id, INTERACTIVE ops/mem, 16-byte random salt | libsodium `crypto_pwhash_*` |
| Private key wrapping | AES-256-GCM over X25519 sk; envelope = nonce \|\| ct \|\| tag | libsodium `crypto_aead_aes256gcm_*` |
| Transport | HTTPS/TLS 1.2+ | Raw OpenSSL (libssl + libcrypto) |
| Cert validation | Pinned self-signed cert (only the pinned cert is trusted; Windows CA store is bypassed) | OpenSSL `SSL_CTX_load_verify_locations` + `SSL_set1_host` |

**Notes:**
- AES-256-GCM requires hardware AES-NI (Intel/AMD); `Client` constructor throws `std::runtime_error` if unavailable
- Nonce scheme: fresh 96-bit nonce drawn from `randombytes_buf` on every `encryptAes256Gcm` call — no counter, no state carried between messages
- Associated data (sender_id, recipient_id, message_id, timestamp) is bound into the AEAD tag but not encrypted — any tampering causes decryption to fail
- HPKE-style key establishment: each message derives a fresh AES key via `DH1 = X25519(eph_sk, recipient_pk)` and `DH2 = X25519(sender_sk, recipient_pk)`; IKM = `DH1 || DH2`; salt = `eph_pk`; HKDF-Extract → PRK; HKDF-Expand(PRK, info) → 32-byte AES key; `eph_pk` travels in the `ephemeral_pk` wire field (hex-encoded)
- Private keys are stored server-side wrapped under a password-derived KEK (Argon2id → AES-256-GCM); the server never holds a plaintext private key
- TLS uses cert pinning: `certs/server.crt` is loaded as the sole trust anchor via `SSL_CTX_load_verify_locations`; the Windows system CA store is intentionally bypassed so only the pinned cert is trusted
- Hostname verification enforced via `SSL_set1_host` — connections rejected on CN/SAN mismatch
- Plain HTTP rejected — this client is HTTPS only

---

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
