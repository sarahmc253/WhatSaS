# WhatSaS — Web Client

A secure end-to-end encrypted messaging web client built with vanilla JavaScript and the Web Crypto API. All encryption and decryption happens entirely in the browser — the server only ever sees ciphertext.

---

## Prerequisites

Before setting up the web client, ensure you have:

- **Node.js** v18 or later — [download here](https://nodejs.org)
- **npm** (comes with Node.js)
- The **WhatSaS Flask server** running (see `server/README.md`)
- A modern browser (Chrome 120+, Firefox 121+, Edge 120+)

---

## Installation

### Step 1 — Install dependencies

Open a terminal, navigate to the `client-web` directory, and run:

```bash
cd client-web
npm install
```

This installs `argon2-browser` (used for Argon2id password hashing) into `node_modules/`. No build or compile step is needed — the client uses ES modules loaded directly by the browser.

### Step 2 — Add background image (optional)

The chat background uses `bows_background.png`. If you have this file, place it at:

```text
client-web/images/bows_background.png
```

The app works without it — the background falls back to a plain pink gradient.

### Step 3 — Start the backend server

The web client has no standalone server of its own. It is served as static files by the Flask backend. Start the server from the project root:

```bash
cd server
python run.py
```

See `server/README.md` for full server setup instructions including environment variables and database setup.

---

## Running the App

Once the Flask server is running, open your browser and navigate to:

```text
https://sas.theburkenator.com
```

For **local development** (Flask with SSL context on port 5000):

```text
https://localhost:5000
```

That's it — no separate build or bundling step required.

---

## Project Structure

```text
client-web/
├── index.html                  # App shell — all views rendered dynamically by JS
├── package.json                # npm dependencies
├── node_modules/               # Installed by npm install
│   └── argon2-browser/
├── css/
│   ├── base.css                # Reset, CSS variables, navbar, layout, buttons, forms
│   ├── auth.css                # Login, register, unlock views + password rule checklist
│   ├── chat.css                # Two-column chat layout, message bubbles, send bar
│   └── verify.css              # Blockchain verification view
├── js/
│   ├── app.js                  # Router and navbar — controls which view is shown
│   ├── api.js                  # All HTTP calls to the backend (fetch wrapper + session state)
│   └── views/
│       ├── auth.js             # Login, register, unlock screens
│       ├── inbox.js            # Chat threads, send, forward, revoke, delete
│       └── helpers.js          # Shared utilities (escaping, crypto helpers, TOFU, validation)
├── crypto/
│   ├── constants.js            # HKDF domain separation info strings
│   ├── hkdf.js                 # HKDF-Extract and HKDF-Expand (Web Crypto)
│   ├── keypair.js              # X25519 keypair generation
│   ├── keyStorage.js           # Private key wrap/unwrap (Argon2id KEK + AES-GCM)
│   └── messageEncryption.js    # HPKE-Auth encrypt/decrypt
├── blockchain/
│   └── blockchainVerifyView.js # Standalone Merkle root verification page (no login needed)
└── images/
    └── bows_background.png     # Chat area background (optional)
```

---

## Features

| Feature | Notes |
|---|---|
| Sign up | Password must be 8+ chars with uppercase, lowercase, number, and special character |
| Sign in | JWT stored in sessionStorage — cleared when tab closes |
| Change password | Re-wraps private key under the new password, same strength rules |
| Conversations | WhatsApp-style: contacts list on the left, active thread on the right |
| Send messages | Encrypted client-side with HPKE-Auth before leaving the browser |
| Receive & decrypt | Decrypted in the browser — server never sees plaintext |
| Forward | Re-encrypts for a new recipient; shows their key fingerprint first |
| Revoke | Blocks recipient's access; message stays visible to sender with "Revoked" badge |
| Delete | Hard-deletes from the database; sender only |
| Download | Saves message as a `.txt` file |
| Key fingerprints | Shown in thread header for out-of-band identity verification |
| TOFU pinning | First-seen key pinned in `localStorage`; key changes trigger a warning |
| Blockchain verify | Public page at `#verify` — no login required |

---

## Cryptographic Summary

For the full threat model and primitive justifications see the cryptographic design document (to be submitted alongside the project).

| Layer | Primitive |
|---|---|
| Key exchange | DHKEM(X25519) — two DH operations (sender auth) |
| Key derivation | HKDF-SHA256 (RFC 5869) |
| Encryption | AES-256-GCM with associated data |
| Associated data | `{sender_id, recipient_id, message_id, timestamp}` (canonical JSON, no spaces) |
| Password hashing | Argon2id (m=32768 KiB, t=2, p=4) |
| Key at rest | AES-256-GCM wrapped private key |

---

## Troubleshooting

**Login page not visible / blank screen**
- Open the browser DevTools console (`F12`) and check for JavaScript errors
- Ensure the Flask server is running and accessible

**Messages show as `(encrypted)`**
- Both clients (web and C++) use the same wire format and key encoding — accounts created on either client are interoperable
- The exception is the C++ Argon2id fallback path, which does not interoperate with the web client's key derivation
- Check that `sender_x25519_public_key` is present in the server's `/messages` response

**`argon2-browser` not found**
- Run `npm install` inside the `client-web/` directory

**CORS errors on the blockchain verify page**
- The Sepolia RPC endpoint (`sepolia.gateway.tenderly.co`) must be CORS-enabled; the one configured in `index.html` supports browser requests

---

## Running the verify page locally

To serve the verify page without the Flask backend, use Python's built-in HTTP server:

```bash
cd client-web
python3 -m http.server 8080
```

Then open [http://localhost:8080/verify.html](http://localhost:8080/verify.html) in your browser.
