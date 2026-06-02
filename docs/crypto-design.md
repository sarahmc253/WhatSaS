# WhatSaS Crypto Design

## Registration

Both clients generate an X25519 keypair, derive a Key Encryption Key (KEK) from the
user's password using Argon2id, wrap a random Data Encryption Key (DEK) with that KEK,
encrypt the private key with the DEK, and POST the public key plus the wrapped private
key envelope to `/auth/register`.  The server stores the public key in the key registry
and the wrapped private key blob for later retrieval at login.  The plaintext private key
and the password never leave the client.

---

### Key derivation chain (shared by both clients)

```
password + salt [16 B random]
        │
        ▼  Argon2id  (mem = 32 MB · time = 2 · p = 4 · hashLen = 32)
       KEK [32 B]
        │
        ▼  HKDF-SHA-256  (info = "securemsg-dek-wrap-v1"  ·  salt = salt above)
    wrapKey [32 B]
        │
        ├──────────────────────────────────────────────────────────────────┐
        │                                                                  │
        ▼  AES-256-GCM  (nonce = kekNonce [12 B])                         │
  wrappedDek  ◄──── random DEK [32 B]                                     │
                             │                                             │
                             ▼  AES-256-GCM  (nonce = dekNonce [12 B])    │
                       ciphertext  ◄──── private key bytes                │
                                                                           │
Wire envelope ─────────────────────────────────────────────────────────────┘
  base64( JSON({ salt, kek_nonce, wrapped_dek, dek_nonce, ciphertext }) )
```

---

### C++ client registration flow

```mermaid
flowchart TD
    A([User enters username · email · password]) --> B

    subgraph KG["① Keypair generation — libsodium"]
        B["crypto_kx_keypair()\n───────────────────\npk  32 B raw\nsk  32 B raw"]
    end

    B --> C
    B --> PK["base64(pk)\n→ x25519_public_key"]

    subgraph W["② Private-key wrapping — key_wrap.hpp"]
        C["randombytes_buf\n→ salt [16 B]"] --> D
        D["crypto_pwhash  (Argon2id)\nmem=32 MB · time=2 · p=4\n─────────────────────────\nKEK [32 B]"] --> E
        E["HKDF-SHA-256\ninfo = 'securemsg-dek-wrap-v1'\nsalt = salt\n──────────────────────────\nwrapKey [32 B]"] --> F
        F["randombytes_buf → DEK [32 B]\nrandombytes_buf → kekNonce [12 B]"] --> G
        G["AES-256-GCM  (OpenSSL EVP)\nkey=wrapKey · nonce=kekNonce · pt=DEK\n────────────────────────────────────\nwrappedDek [48 B: 32 + 16-tag]"] --> H
        H["randombytes_buf\n→ dekNonce [12 B]"] --> I
        I["AES-256-GCM  (OpenSSL EVP)\nkey=DEK · nonce=dekNonce · pt=sk\n─────────────────────────────────\nciphertext [48 B: 32 + 16-tag]"] --> J
        J["JSON { salt · kek_nonce · wrapped_dek\n        dek_nonce · ciphertext }  (all base64)\n──────────────────────────────────────────\nbase64(JSON) → wrapped_private_key"]
    end

    J --> REG
    PK --> REG

    subgraph REG["③ Server submission — POST /auth/register"]
        R["{ username · password · email\n  x25519_public_key\n  wrapped_private_key\n  kek_salt = base64(salt) }"]
    end
```

---

### Web client registration flow

```mermaid
flowchart TD
    A([User enters username · email · password]) --> B

    subgraph KG["① Keypair generation — Web Crypto API"]
        B["crypto.subtle.generateKey\n{ name: 'X25519' · extractable: true }\n───────────────────────────────────────\npk  CryptoKey\nsk  CryptoKey"] --> EX
        EX["exportKey('raw',   pk) → pk_bytes [32 B]\nexportKey('pkcs8', sk) → sk_bytes [~119 B PKCS#8 DER]"]
    end

    EX --> C
    EX --> PK["getPublicKeyB64(pk)\n→ x25519_public_key"]

    subgraph W["② Private-key wrapping — keyStorage.js"]
        C["crypto.getRandomValues\n→ salt [16 B]"] --> D
        D["argon2.hash  (Argon2id)\nmem=32 MB · time=2 · p=4 · hashLen=32\n──────────────────────────────────────\nKEK raw bytes [32 B]"] --> E
        E["importKeyMaterial(KEK)\nhkdfDerive(kekMaterial,\n  'securemsg-dek-wrap-v1', salt, 32)\n─────────────────────────────────────\nwrapKey CryptoKey (AES-GCM · non-extractable)"] --> F
        F["getRandomValues → DEK [32 B]\ngetRandomValues → kekNonce [12 B]\nimportKey('raw', DEK, 'AES-GCM')"] --> G
        G["crypto.subtle.wrapKey\n'raw' · dekKey · wrapKey\n{ AES-GCM · iv=kekNonce }\n──────────────────────────\nwrappedDek"] --> H
        H["getRandomValues\n→ dekNonce [12 B]"] --> I
        I["crypto.subtle.encrypt\n{ AES-GCM · iv=dekNonce } · dekKey · sk_bytes\n─────────────────────────────────────────────\nciphertext"] --> J
        J["EncryptedPrivateKey.toJSON()\n{ salt · kek_nonce · wrapped_dek\n  dek_nonce · ciphertext }  (all base64)\n──────────────────────────────────────\nbase64(JSON) → wrapped_private_key"]
    end

    J --> REG
    PK --> REG

    subgraph REG["③ Server submission — POST /auth/register"]
        R["{ username · password · email\n  x25519_public_key\n  wrapped_private_key\n  kek_salt = base64(salt) }"]
    end
```

---

### Parameter reference

| Parameter | Value | Where set |
|-----------|-------|-----------|
| Argon2id memory | 32 MB (32 768 KiB) | `key_wrap.hpp` · `kek.js` |
| Argon2id iterations | 2 | `key_wrap.hpp` · `kek.js` |
| Argon2id parallelism | 4 | `key_wrap.hpp` · `kek.js` |
| KEK length | 32 B | `KEK_LEN` · `hashLen` |
| Salt length | 16 B | `SALT_LEN` · `generateSalt()` |
| HKDF hash | SHA-256 | `crypto_utils.hpp` · `hkdf.js` |
| HKDF info (DEK wrap) | `"securemsg-dek-wrap-v1"` | `hkdf_info.hpp` · `constants.js` |
| DEK length | 32 B | `DEK_LEN` · inline |
| AES-GCM nonce | 12 B | `NONCE_LEN` · `getRandomValues` |
| AES-GCM tag | 16 B | OpenSSL default · Web Crypto default |

---

### Interoperability note

The outer envelope format and all Argon2id / HKDF parameters are identical between the
two clients.  The inner plaintext differs:

| | C++ client | Web client |
|---|---|---|
| Private key format | Raw 32 B (`crypto_kx_keypair`) | PKCS#8 DER ~119 B (`exportKey('pkcs8', ...)`) |
| AES-256-GCM implementation | OpenSSL `EVP_EncryptInit_ex` | `crypto.subtle.wrapKey` / `encrypt` |
| HKDF implementation | libsodium HMAC-SHA-256 (`hkdfExtract` / `hkdfExpand32`) | `crypto.subtle.deriveBits` (HKDF) |

Because the decrypted private key byte lengths differ (32 B vs ~119 B), a wrapped key
produced by one client cannot be unwrapped by the other.  Each client validates the
length of the recovered key before use and will reject the other's format.

---

## Key Publication and TOFU Pinning

### Overview

Alice's public key reaches the server via two paths:

1. **At registration** — included in the `POST /auth/register` body as `x25519_public_key`.
2. **At every login** — `POST /keys` is called immediately after login in case the registry
   entry was lost (e.g. server database restore).  The server treats this as an upsert.

Bob fetches Alice's key on first message via `GET /users/alice`, then pins it locally.
All subsequent fetches compare the server response against the pin; any deviation is
treated as a potential key-substitution attack and rejected before the key is used.

---

### Flow diagram

```mermaid
flowchart TD
    subgraph ALICE["Alice — key publication"]
        A1["POST /auth/register\nbody includes x25519_public_key: base64(pk)"]
        A2["POST /keys\n{ user_id, public_key: base64(pk) }\ncalled at every login\n(ensures registry entry exists)"]
    end

    subgraph SRV["Server — key registry"]
        S["Stores username → x25519_public_key\n(public keys are not secret;\nserver cannot derive private key)"]
    end

    A1 --> S
    A2 --> S

    subgraph BOB["Bob — fetchPeerPublicKey  (Client.cpp)"]
        B1["GET /users/alice\nAuthorization: Bearer token"] --> B2
        B2["Decode x25519_public_key\nValidate: exactly 32 B"] --> B3
        B3{"alice in\npinnedKeys_?"}

        B3 -- "No (first contact)" --> P1
        subgraph TOFU["TOFU pin — first contact"]
            P1["Write whatsas_pins.txt\nformat: username  base64(pk)  uuid"] --> P2
            P2{"Write\nsucceeded?"}
            P2 -- Yes --> P3["Accept pk\nStore in pinnedKeys_ + pinnedIds_"]
            P2 -- No  --> P4["Erase in-memory entry\nReturn empty — do not use key"]
        end

        B3 -- "Yes — key matches pin" --> U1
        subgraph UUID["UUID consistency check"]
            U1{"UUID changed\nfor same username?"}
            U1 -- No  --> ACC["Accept pk\n(use cached pin)"]
            U1 -- Yes --> REJ2["Reject\nlog: possible account takeover"]
        end

        B3 -- "Yes — key differs from pin" --> REJ1["Reject\nlog: possible key substitution attack\nReturn empty vector — caller aborts send"]
    end

    S --> B1
    P3 --> USE["Encrypt message\nto Alice's pk"]
    ACC --> USE
```

---

### Pin file format

```
# WhatSaS TOFU key pins — do not edit manually
alice   base64(alice_pk_32B)   550e8400-e29b-41d4-a716-446655440000
bob     base64(bob_pk_32B)     6ba7b810-9dad-11d1-80b4-00c04fd430c8
```

One line per peer: `username  base64(pk)  server-uuid`.  The UUID column was added
after initial release; lines with only two tokens load without a UUID (remap check
activates once the UUID is next seen from the server).

---

### Trust model

| Threat | Can server do it? | Why / mitigation |
|--------|-------------------|------------------|
| Read plaintext messages | **No** | Messages are AES-256-GCM encrypted to the recipient's X25519 public key; the server never holds a private key |
| Read the wrapped private key | **No** | Blob is AES-256-GCM encrypted under a password-derived key (Argon2id); password never sent to server |
| Forge a message from Alice to Bob | **No** | Encryption uses Alice's static private key (DHKEM); server does not have it |
| Substitute Alice's public key before Bob's first contact | **Yes (first-contact only)** | A compromised server can serve a different key to Bob on the very first `GET /users/alice`; after that, the TOFU pin prevents substitution |
| Substitute Alice's key after Bob has pinned it | **No** | Client rejects any key that differs from the stored pin and logs a warning |
| Remap Alice's username to a different account UUID | **No** | Client detects UUID change for a pinned username and rejects the response |
| Prevent communication (denial of service) | **Yes** | Server controls message delivery; this is out of scope for the crypto layer |

The first-contact window is the sole residual trust assumption.  Users who need stronger
guarantees should verify Alice's public key fingerprint out-of-band before sending the
first message.

---

## Send Message

### Overview

Every message derives a fresh AES-256-GCM key via a two-DH HPKE-style construction
(DHKEM(X25519, HKDF-SHA256)).  The ephemeral private key is zeroized immediately after
the two DH operations; the AES key is zeroized after encryption.  The server receives
only the ciphertext, the ephemeral public key, and the nonce — never any key material.

### DHKEM key derivation (both clients)

```
Sender holds:  sender_sk  (long-term, 32 B)
               recipient_pk (fetched + TOFU-pinned, 32 B)

① fresh eph_sk / eph_pk  (32 B each, CSPRNG)

② DH1 = X25519(eph_sk,    recipient_pk)   [32 B]  — forward secrecy
   DH2 = X25519(sender_sk, recipient_pk)   [32 B]  — sender authentication

   eph_sk  zeroized immediately after ②

③ low-order check: abort if DH1 = 0 or DH2 = 0

④ IKM = DH1 || DH2  [64 B]  →  zeroize DH1, DH2

⑤ PRK = HKDF-Extract(salt = eph_pk, IKM)  →  zeroize IKM

⑥ AES_KEY = HKDF-Expand(PRK, info = "securemsg-msg-enc-v1")  [32 B]  →  zeroize PRK

⑦ nonce [12 B] = CSPRNG

⑧ ciphertext+tag = AES-256-GCM(AES_KEY, nonce, plaintext [, AD])  →  zeroize AES_KEY

Wire fields sent to POST /messages:
  ephemeral_pk  base64(eph_pk)
  nonce         base64(nonce)
  ciphertext    base64(ciphertext + 16-byte tag)
  recipient_id  server UUID (from TOFU pin)
```

---

### C++ client send flow

```mermaid
flowchart TD
    A([Alice types plaintext · selects recipient]) --> B

    subgraph FETCH["① Fetch + pin recipient key  (see Key Publication)"]
        B["fetchPeerPublicKey(recipientUsername)\nGET /users/{username}\n────────────────────────────────\nrecipient_pk [32 B]  ·  uuid"]
    end

    B --> C

    subgraph HPKE["② DHKEM  —  hpke_utils.hpp · hpkeSend()"]
        C["crypto_box_keypair()\n→ eph_pk [32 B]  ·  eph_sk [32 B]"] --> D
        D["DH1 = crypto_scalarmult(eph_sk, recipient_pk)\nDH2 = crypto_scalarmult(sender_sk, recipient_pk)\nsodium_memzero(eph_sk) immediately"] --> E
        E{"DH1 = 0\nor DH2 = 0?"}
        E -- Yes --> ABORT["Abort — low-order point\nreturn nullopt"]
        E -- No  --> F
        F["IKM = DH1 || DH2  [64 B]\nzeroize DH1, DH2"] --> G
        G["HKDF-Extract\nsalt=eph_pk · IKM\n→ PRK [32 B]\n(HMAC-SHA256 · libsodium)"] --> H
        H["HKDF-Expand\nPRK · info='securemsg-msg-enc-v1'\n→ AES_KEY [32 B]\nzeroize PRK"]
    end

    H --> I

    subgraph ENC["③ AES-256-GCM encrypt  —  message_crypto.hpp · encryptMessage()"]
        I["generateMsgId() → msgId [32-char hex, CSPRNG]\nts = time(nullptr)"] --> J
        J["AD = JSON { sender_id · recipient_id\n             message_id · timestamp }"] --> K
        K["nonce [12 B] = CSPRNG\ncrypto_aead_aes256gcm_encrypt\n  (libsodium · AES-NI required)\n──────────────────────────────\nnonce || ciphertext+tag [16 B]"] --> L
        L["sodium_memzero(AES_KEY)\nbase64(nonce) → nonceB64\nbase64(ct+tag) → ctB64"]
    end

    B --> M
    L --> POST

    subgraph POST["④ POST /messages"]
        M["recipient_id = uuid (from pin)"]
        P["{ recipient_id · ciphertext: ctB64\n  nonce: nonceB64\n  ephemeral_pk: base64(eph_pk) }"]
        M --> P
    end

    L --> P
    C --> P2["ephemeral_pk\n→ base64(eph_pk)"]
    P2 --> P
```

---

### Web client send flow

```mermaid
flowchart TD
    A([Alice types plaintext · selects recipient]) --> B

    subgraph FETCH["① Fetch recipient key"]
        B["GET /users/{username}\n────────────────────────────\nrecipient_pk CryptoKey [32 B]"]
    end

    B --> C

    subgraph HPKE["② DHKEM  —  messageEncryption.js · encryptMessage()"]
        C["generateKeypair()\n→ eph_pk CryptoKey  ·  eph_sk CryptoKey\n(Web Crypto · X25519 · extractable)"] --> D
        D["DH1 = crypto.subtle.deriveBits\n  { X25519 · public: recipient_pk } · eph_sk  →  32 B\nDH2 = crypto.subtle.deriveBits\n  { X25519 · public: recipient_pk } · sender_sk  →  32 B"] --> F
        F["IKM = DH1 || DH2  [64 B]\nephPkBytes = exportKey('raw', eph_pk)  [32 B]"] --> G
        G["importKeyMaterial(IKM)\nhkdfDerive(keyMaterial,\n  'securemsg-msg-enc-v1',\n  salt=ephPkBytes, 32)\n──────────────────────────────\nmessageKeyBytes [32 B]"] --> H
        H["importKey('raw', messageKeyBytes, 'AES-GCM')\n→ messageKey CryptoKey (non-extractable)"]
    end

    H --> I

    subgraph ENC["③ AES-256-GCM encrypt"]
        I["nonce [12 B] = crypto.getRandomValues"] --> K
        K["crypto.subtle.encrypt\n{ AES-GCM · iv: nonce }\nmessageKey · TextEncoder(plaintext)\n────────────────────────────────\nciphertext  (includes 16-byte tag)"]
    end

    K --> POST

    subgraph POST["④ POST /messages"]
        P["{ recipient_id · ciphertext: base64(ciphertext)\n  nonce: base64(nonce)\n  ephemeral_pk: base64(ephPkBytes) }"]
    end

    C --> P2["ephPkBytes → base64"]
    P2 --> P
```

---

### DHKEM parameter reference

| Parameter | Value | Where set |
|-----------|-------|-----------|
| KEM | DHKEM(X25519) | `hpke_utils.hpp` · `messageEncryption.js` |
| DH function | X25519 (`crypto_scalarmult`) | libsodium · Web Crypto |
| HKDF hash | SHA-256 | `crypto_utils.hpp` · `hkdf.js` |
| HKDF salt | `eph_pk` [32 B] | both clients |
| HKDF info (message key) | `"securemsg-msg-enc-v1"` | `hkdf_info.hpp` · `constants.js` |
| AES key length | 32 B (AES-256) | both clients |
| AES nonce | 12 B (CSPRNG) | both clients |
| AES tag | 16 B (GCM default) | both clients |
| AES implementation | libsodium `crypto_aead_aes256gcm` (AES-NI) | C++ only |
| AES implementation | `crypto.subtle.encrypt` (AES-GCM) | web only |
| Forward secrecy | Per-message ephemeral keypair | both clients |

---

### Cross-client interoperability note

The DHKEM construction and all key derivation parameters are identical, so both clients
produce the same AES key from the same inputs.  However, the AES-GCM **additional data
(AD)** usage differs:

| | C++ client | Web client |
|---|---|---|
| AD bound into AEAD tag | `JSON { sender_id, recipient_id, message_id, timestamp }` | none — `crypto.subtle.encrypt` called without `additionalData` |
| AES-GCM implementation | libsodium `crypto_aead_aes256gcm_encrypt` (requires AES-NI) | `crypto.subtle.encrypt` |

Because the AD is mixed into the authentication tag, a ciphertext produced by the C++
client will fail AEAD verification when the web client tries to decrypt it (no AD
provided), and vice versa.  The two clients currently form separate ciphertext domains
and cannot decrypt each other's messages.

---

## Receive Message

### Overview

The recipient re-derives the same per-message AES key using their own long-term private
key, the sender's long-term public key (TOFU-pinned), and the ephemeral public key
carried in the wire message.  X25519 symmetry guarantees the two DH outputs are
identical to those the sender computed, so the same HKDF chain produces the same
AES key.  No key material is transmitted; the server cannot decrypt.

### DHKEM key recovery (both clients)

```
Recipient holds:  recipient_sk  (long-term, 32 B)
                  sender_pk     (fetched + TOFU-pinned, 32 B)

Wire fields from GET /messages:
  eph_pk       base64 → [32 B]   (kem_output sent by sender)
  nonce        base64 → [12 B]
  ciphertext   base64 → [N + 16 B tag]

① DH1 = X25519(recipient_sk, eph_pk)    ≡ sender's X25519(eph_sk, recipient_pk)
   DH2 = X25519(recipient_sk, sender_pk) ≡ sender's X25519(sender_sk, recipient_pk)

   (X25519 symmetry: X25519(a, B·G) = X25519(b, A·G) = a·b·G)

② low-order check: abort if DH1 = 0 or DH2 = 0

③ IKM = DH1 || DH2  [64 B]  →  zeroize DH1, DH2

④ PRK = HKDF-Extract(salt = eph_pk, IKM)  →  zeroize IKM

⑤ AES_KEY = HKDF-Expand(PRK, info = "securemsg-msg-enc-v1")  [32 B]  →  zeroize PRK

⑥ plaintext = AES-256-GCM-Decrypt(AES_KEY, nonce, ciphertext [, AD])  →  zeroize AES_KEY
```

---

### C++ client receive flow

```mermaid
flowchart TD
    A([Bob selects a conversation · peer username known]) --> B

    subgraph FETCH["① GET /messages  —  Client::getMessages()"]
        B["GET /messages\nAuthorization: Bearer token\n────────────────────────────\nJSON array of message objects"]
    end

    B --> C

    subgraph PARSE["② Per-message validation  —  Client::receiveMessages()"]
        C["Check required fields:\nid · sender_id · nonce · ciphertext\nephemeral_pk · created_at"] --> D
        D["base64-decode ephemeral_pk\nValidate: exactly 32 B"] --> E
        E["Parse created_at ISO-8601\n→ time_t timestamp\n(_mkgmtime on Win32 · timegm on POSIX)"]
    end

    E --> F

    subgraph HPKE["③ DHKEM key recovery  —  hpke_utils.hpp · hpkeReceive()"]
        F["DH1 = crypto_scalarmult(recipient_sk, eph_pk)\nDH2 = crypto_scalarmult(recipient_sk, sender_pk)"] --> G
        G{"DH1 = 0\nor DH2 = 0?"}
        G -- Yes --> SKIP["Skip message\nlog: HPKE receive failed"]
        G -- No  --> H
        H["IKM = DH1 || DH2  [64 B]\nzeroize DH1, DH2"] --> I
        I["HKDF-Extract\nsalt=eph_pk · IKM\n→ PRK [32 B]\n(HMAC-SHA256 · libsodium)"] --> J
        J["HKDF-Expand\nPRK · info='securemsg-msg-enc-v1'\n→ AES_KEY [32 B]\nzeroize PRK"]
    end

    J --> K

    subgraph DEC["④ AES-256-GCM decrypt  —  message_crypto.hpp · decryptMessage()"]
        K["Reconstruct AD = JSON\n{ sender_id · recipient_id\n  message_id · timestamp }"] --> L
        L["noncePlusCt = base64(nonce) || base64(ciphertext)\ncrypto_aead_aes256gcm_decrypt\n  (libsodium · AES-NI required)\n──────────────────────────────\nplaintext bytes"] --> M
        M["sodium_memzero(AES_KEY)\nDecryptedMessage { senderId · plaintext · timestamp }"]
    end

    M --> N

    subgraph STORE["⑤ Store + display"]
        N["store.addMessage(msg, peerKey)\nconv.addMessage(decryptedMsg)"] --> O
        O["printConversation(conv)\n[YYYY-MM-DD HH:MM:SS] sender: plaintext"]
    end
```

---

### Web client receive flow

```mermaid
flowchart TD
    A([Bob opens conversation]) --> B

    subgraph FETCH["① GET /messages"]
        B["GET /messages\nAuthorization: Bearer token\n────────────────────────────\nJSON array of message objects"]
    end

    B --> C

    subgraph HPKE["② DHKEM key recovery + decrypt  —  messageEncryption.js · decryptMessage()"]
        C["base64-decode: ciphertext · nonce · ephemeral_pk\nimport ephPk as CryptoKey (X25519 · raw)"] --> D
        D["DH1 = crypto.subtle.deriveBits\n  { X25519 · public: eph_pk } · recipient_sk  →  32 B\nDH2 = crypto.subtle.deriveBits\n  { X25519 · public: sender_pk } · recipient_sk  →  32 B"] --> F
        F["IKM = DH1 || DH2  [64 B]\nephPkBytes = exportKey('raw', eph_pk)  [32 B]"] --> G
        G["importKeyMaterial(IKM)\nhkdfDerive(keyMaterial,\n  'securemsg-msg-enc-v1',\n  salt=ephPkBytes, 32)\n──────────────────────────────\nmessageKeyBytes [32 B]"] --> H
        H["importKey('raw', messageKeyBytes, 'AES-GCM')\n→ messageKey CryptoKey (non-extractable)"] --> I
        I["crypto.subtle.decrypt\n{ AES-GCM · iv: nonce }\nmessageKey · ciphertext\n────────────────────────────\nplaintext (no AD checked)"]
    end

    I --> J

    subgraph DISPLAY["③ Display"]
        J["TextDecoder.decode(plaintext)\nRender in conversation UI"]
    end
```

---

### Security properties

| Property | Mechanism |
|----------|-----------|
| Confidentiality | AES-256-GCM; only the recipient's private key can recover DH1 |
| Sender authentication | DH2 = X25519(recipient_sk, sender_pk); a forged message cannot reproduce DH2 without sender_sk |
| Forward secrecy | Each message uses a fresh ephemeral keypair; compromise of long-term keys does not expose past messages |
| Integrity (C++ client) | AEAD tag covers ciphertext + AD (sender, recipient, message ID, timestamp); any field tampering causes decryption failure |
| Integrity (web client) | AEAD tag covers ciphertext only; AD not used — sender/recipient/timestamp fields are not authenticated by the tag |
| Key zeroization | `sodium_memzero` on eph_sk, DH1, DH2, IKM, PRK, AES_KEY after each use (C++ client) |

---

## At-Rest Storage

### Overview

The X25519 private key is **never written to disk in plaintext** by either client.  The
only persisted form is the wrapped envelope, which lives in the server database.  Even
with full read access to the database, an attacker cannot recover the private key without
the user's password — the password is never transmitted to or stored by the server.

Local disk writes per client:

| Client | What is written to disk | Where |
|--------|------------------------|-------|
| C++ | TOFU public-key pins only (no private key) | `whatsas_pins.txt` |
| Web | JWT in `sessionStorage` only (cleared on tab close); private key held in JS heap only | browser session |

### Wrapped key envelope (server database)

The server stores three fields per user in the `users` table:

```
users table row
├── x25519_public_key  ──  base64(pk [32 B])        ← public; not secret
├── kek_salt           ──  base64(salt [16 B])       ← public Argon2id salt
└── wrapped_private_key ── base64(JSON envelope)    ← encrypted blob

JSON envelope (inner, before outer base64):
{
  "salt":        base64( salt       [16 B] )   ← Argon2id salt (same as kek_salt)
  "kek_nonce":   base64( kekNonce   [12 B] )   ← AES-GCM nonce for DEK wrap
  "wrapped_dek": base64( wrappedDek [48 B] )   ← AES-GCM(wrapKey, kekNonce, DEK) + 16-byte tag
  "dek_nonce":   base64( dekNonce   [12 B] )   ← AES-GCM nonce for private key
  "ciphertext":  base64( ct         [?B]  )    ← AES-GCM(DEK, dekNonce, privateKey) + 16-byte tag
}
```

C++ ciphertext is 48 B (32 B raw key + 16 B tag).
Web ciphertext is ~135 B (~119 B PKCS#8 DER key + 16 B tag).

### Key derivation and wrapping diagram

```mermaid
flowchart TD
    P(["User password\n(never leaves client,\nnever stored)"])

    subgraph KD["Key derivation — identical on both clients"]
        P --> A["Argon2id\nmem=32 MB · time=2 · p=4\nsalt [16 B random]\n──────────────────\nKEK [32 B]"]
        A --> B["HKDF-SHA-256\ninfo='securemsg-dek-wrap-v1'\nsalt=salt\n─────────────────────\nwrapKey [32 B]"]
    end

    subgraph WRAP["Two-layer wrapping"]
        B --> C["AES-256-GCM\nkey=wrapKey · nonce=kekNonce\nplaintext=DEK [32 B random]\n──────────────────────────\nwrappedDek [48 B]"]
        C --> E
        D["private key bytes\n(32 B raw — C++)\n(~119 B PKCS#8 — web)"] --> F["AES-256-GCM\nkey=DEK · nonce=dekNonce\nplaintext=privateKey\n─────────────────────\nciphertext"]
        F --> E
    end

    subgraph ENV["JSON envelope → base64 → server DB"]
        E["{ salt · kek_nonce · wrapped_dek\n  dek_nonce · ciphertext }\n─────────────────────────────────────────────\nbase64(JSON) → stored as wrapped_private_key"]
    end
```

### In-memory lifecycle at login

```mermaid
flowchart TD
    A(["User enters password"]) --> B

    subgraph FETCH["Fetch from server"]
        B["POST /auth/login\n────────────────────────────\nreceive: token · wrapped_private_key\n         kek_salt · x25519_public_key"]
    end

    B --> C

    subgraph UNWRAP["Unwrap private key in memory"]
        C["base64-decode → parse JSON envelope\n→ EncryptedPrivateKey fields"] --> D
        D["Argon2id(password, salt) → KEK\nHKDF(KEK, 'securemsg-dek-wrap-v1', salt) → wrapKey"] --> E
        E["AES-GCM-Decrypt(wrapKey, kekNonce, wrappedDek)\n→ DEK [32 B]"] --> F
        F["AES-GCM-Decrypt(DEK, dekNonce, ciphertext)\n→ privateKey bytes"]
    end

    F --> G

    subgraph MEM["In-memory only — never written to disk"]
        G["C++: raw bytes in Client::staticSk_ vector\n(freed when Client is destroyed)"]
        H["Web: non-extractable CryptoKey (X25519)\nimported via crypto.subtle.importKey('pkcs8', ...)\n(cleared on logout / tab close)"]
    end
```

### What a database compromise reveals

| Data item | Stored on server | Recoverable by attacker with DB access |
|-----------|-----------------|----------------------------------------|
| X25519 public key | Yes (plaintext) | Yes — public by design |
| Argon2id salt | Yes (plaintext) | Yes — needed to attempt offline dictionary attack |
| Wrapped DEK | Yes (ciphertext) | Only if password is guessed; Argon2id (32 MB / 2 iterations) makes brute-force expensive |
| Private key | No — only the AES-GCM ciphertext | Only after unwrapping DEK; requires password |
| Plaintext messages | No — ciphertext only | Never; requires recipient private key |
| User password | Never stored | Never — only a hash is stored for login authentication |

The sole offline attack surface is a dictionary attack on `wrapped_private_key` guarded
by Argon2id (32 MB memory, 2 iterations, parallelism 4).  A weak password remains the
primary residual risk.
