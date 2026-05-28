# WhatSaS

Secure messaging application — CS4455 Cybersecurity Epic 2026, Group sas.

**Team:** Sarah McDonagh, Sreejita Saha, Aoibheann Mangan

## Project Structure

```
WhatSaS/
├── client.cpp/      # C++ client
├── client-web/      # Web client (HTML/JS)
├── server/          # Python backend
├── contracts/       # Solidity smart contract
├── verification/    # Blockchain verification page
└── docs/            # Design documents and reports
```

## Setup & Installation

### Python dependencies

```bash
pip install -r requirements.txt
```

Covers the server and the `crypto-library` package. Key cryptographic dependencies:

- **`cryptography`** — PyCA's cryptography library; provides X25519 key exchange and HPKE primitives. Actively maintained, audited, and the de-facto standard for Python cryptography.
- **`argon2-cffi`** — Python bindings for the reference Argon2 implementation. Argon2id is the OWASP-recommended password hashing and key-derivation algorithm; this binding uses the same audited C library as the reference spec.

### C++ client

**Ubuntu**

```bash
sudo apt install cmake ninja-build g++ libssl-dev libsodium-dev libsqlite3-dev
```

**macOS**

```bash
brew install cmake ninja openssl libsodium sqlite3
```

**Build** (run from the repo root)

```bash
cmake -B client-cpp/build client-cpp \
  -DSODIUM_INCLUDE=$(brew --prefix libsodium)/include   # macOS only; omit on Ubuntu
cmake --build client-cpp/build
```

**Run**

```bash
./client-cpp/build/sas-client
```

For Windows (MSYS2/UCRT64) and full test instructions see [`client-cpp/README.md`](client-cpp/README.md).

### Memory Ownership

No raw owning pointers (`new`/`delete`) appear in the codebase. Every resource is either owned by a smart pointer or stored by value.

**`HttpClient::ctx_` — `std::unique_ptr<SSL_CTX, SslCtxDeleter>`**
`SSL_CTX` is an opaque OpenSSL type that must be released with `SSL_CTX_free`. A `unique_ptr` with a custom deleter expresses single ownership and guarantees the context is freed when `HttpClient` is destroyed, even if the constructor throws. The deleter is defined in the `.cpp` file so the OpenSSL headers never leak into translation units that include `HttpClient.hpp` — only a forward declaration of `ssl_ctx_st` is needed in the header.

**`UniqueSSL` — `std::unique_ptr<SSL, SslDeleter>`**
A local alias inside `HttpClient.cpp`. Each `get()`/`post()` call wraps the per-request `SSL*` in a `UniqueSSL` so the handle is freed on every exit path — normal return, early error, or exception — without explicit cleanup code at each branch.

**`RaiiSocket` — value-based RAII, no heap allocation**
`SOCKET` (a Win32 integer handle) does not need to live on the heap, so `unique_ptr<SOCKET>` would allocate unnecessarily. Instead, `RaiiSocket` holds the handle as a plain member, calls `closesocket` in its destructor, and deletes copy/move to prevent accidental double-close. The struct lives on the stack for exactly the lifetime of the request.

**Domain objects — value semantics throughout**
`User`, `Message`, `MessageStore`, and `Conversation` own their data via `std::vector` and `std::string` members. No smart pointers are needed because ownership is never shared or transferred — each object is the sole owner of its contents. `Message` is fully immutable after construction; `MessageStore` and `Conversation` grow their collections in place.

**No `shared_ptr`**
There is no shared ownership in the design. Every object has exactly one owner at any point, so `shared_ptr` reference counting would add overhead without benefit.

### STL Containers and Algorithms

#### Containers

| Container | Used in | Why |
|-----------|---------|-----|
| `std::vector<uint8_t>` | `User`, `Message`, `Conversation`, `Client`, `MessageStore` | Contiguous byte storage for keys, ciphertext, and nonces; required by libsodium APIs that take raw `unsigned char*` pointers. |
| `std::vector<Message>` | `MessageStore` | Insertion-order primary store; index-stable and cache-friendly for iteration over all messages. |
| `std::vector<DecryptedMessage>` | `Conversation` | Maintains arrival order; sorted on read rather than on insert so duplicate-check cost stays O(1) via `seenIds_`. |
| `std::string` | All classes | Owning, heap-managed character storage for IDs, URLs, plaintexts, and HTTP data; interoperates directly with nlohmann/json and OpenSSL string APIs. |
| `std::unordered_map<string, vector<Message>>` | `MessageStore::byPeer_` | O(1) average lookup of messages by peer key; peer-keyed access is the hot path when a conversation is opened. |
| `std::unordered_map<string, vector<uint8_t>>` | `Client::pinnedKeys_` | O(1) average lookup for TOFU-pinned public keys; checked on every incoming message so average-case speed matters. |
| `std::unordered_set<std::string>` | `Conversation::seenIds_` | O(1) average duplicate check on every `addMessage` call; `std::set` would give O(log n) for the same lookup. |
| `std::map<string, size_t>` | `MessageStore::getSenderFrequencies` | Return type only; alphabetically sorted output is useful for display, and this path is not performance-critical. |
| `std::optional<Message>` | `MessageStore::findMessage` | Expresses "found or not found" without a sentinel value or heap allocation; cleaner than returning a raw pointer or throwing. |

#### Algorithms

| Algorithm | Used in | Why |
|-----------|---------|-----|
| `std::sort` | `Conversation::getMessages`, `MessageStore::getSortedMessages` | Sorts a copy by timestamp ascending; messages are stored in arrival order and sorted only on read so inserts stay O(1). |
| `std::find_if` | `MessageStore::findMessage`, `MessageStore::hasMessage` | Linear scan with a predicate; no secondary index exists for arbitrary message ID lookup so a scan is the correct tool. |
| `std::any_of` | `MessageStore::hasMessage` | Short-circuits on the first match; avoids scanning the rest of the vector once an ID is found, and returns bool without constructing an `optional`. |
| `std::count_if` | `Conversation::messageCount` | Counts matching messages without a mutable counter variable; intent is expressed directly in the predicate. |
| `std::copy_if` + `std::back_inserter` | `Conversation::getMessagesFromSender` | Filters into a new vector without a manual loop; `back_inserter` removes the need to pre-size the output. |
| `std::transform` | `http_response.cpp::toLower` | Lowercases HTTP header names in place for case-insensitive comparison per RFC 7230; single-pass over the string. |
| `std::move` | `Client` constructor, `Conversation::addMessage`, `HttpClient` move operators | Transfers ownership of key byte vectors and message objects without copying; critical for 32-byte key vectors passed into `Client` at construction. |

### Classes

**`User`** — represents a participant in the system. Holds a user ID, display name, and a Curve25519 (X25519) public key. The local user also stores a private key; remote peers only ever have the public key populated. Keys may be set after construction to support deferred key-registry lookups.

**`Message`** — an immutable snapshot of a single encrypted message in transit or in storage. Holds the AES-256-GCM ciphertext (with 16-byte auth tag), a 12-byte nonce, sender and recipient IDs, a unique message ID, and a Unix timestamp. All fields are set at construction and never mutated.

**`MessageStore`** — in-memory store for received `Message` objects. Maintains a flat insertion-order vector for global access and an unordered map keyed by a canonical peer key (`min(senderId, recipientId)|max(...)`) so both directions of a conversation land in the same bucket. Supports ID-based existence checks, per-peer retrieval, timestamp-sorted views, and sender frequency counts.

**`Conversation`** — organises already-decrypted messages for a single peer. Receives `DecryptedMessage` structs (plaintext, sender, timestamp, message ID) from `Client`, deduplicates by message ID using an unordered set, and returns messages sorted by timestamp. Also caches the peer's X25519 public key after the first TOFU fetch so `Client` does not need to re-fetch it per message.

**`Client`** — the top-level messaging layer. Owns the local user's long-term X25519 keypair, drives HPKE-style key establishment (ephemeral + static DH → HKDF-SHA256 → AES-256-GCM key), and handles TOFU key pinning for peer public keys. Exposes `sendMessage`, `receiveMessages`, `fetchPeerPublicKey`, and `publishPublicKey`, each of which calls through to `HttpClient` for network I/O.

## Key Dates

| Milestone | Date |
|-----------|------|
| Brief released | Mon 18 May 2026 |
| Status report 1 | Fri 23 May 2026, 5:00 PM |
| Status report 2 | Fri 30 May 2026, 5:00 PM |
| **Submission deadline** | **Wed 3 Jun 2026, 5:00 PM** |
| Presentations & interviews | Thu–Fri 4–5 Jun 2026 |

## Submission Checklist

- [ ] All source code committed and zipped
- [ ] README with install, setup, and run instructions
- [ ] Cover document (group name, member IDs, repo URL, contribution breakdown)
- [ ] Cryptographic design document
- [ ] Penetration testing report
- [ ] Deployed smart contract address and ABI
- [ ] AI prompt artefacts (screenshots/logs + reflective commentary)
