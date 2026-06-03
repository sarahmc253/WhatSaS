# WhatSaS

Secure end-to-end encrypted messaging application built for CS4455 Cybersecurity Epic 2026, Group sas.

Messages are encrypted client-side using AES-256-GCM with X25519 key exchange — the server never sees plaintext. Message digests are periodically anchored to the Ethereum Sepolia testnet for tamper-evident integrity verification.

**Team:** Sarah McDonagh (24403067), Sreejita Saha, Aoibheann Mangan  
**GitHub:** https://github.com/sarahmc253/WhatSaS

---

## Project Structure

```
WhatSaS/
├── client-cpp/      # C++ client
├── client-web/      # Web client (HTML/JS)
├── server/          # Python backend
├── contracts/       # Solidity smart contract
├── verification/    # Blockchain verification page
└── docs/            # Design documents and reports
```

---

## Dependencies

| Dependency | Version | Component | Purpose |
|---|---|---|---|
| Python | 3.10+ | Server, crypto-library | Runtime for the Flask API and crypto utilities |
| MySQL | 8.0+ | Server | Stores users, messages, audit log, and blockchain records |
| flask | ≥ 3.1.3 | Server | HTTP API framework |
| flask-jwt-extended | ≥ 4.7.4 | Server | JWT authentication middleware |
| mysql-connector-python | ≥ 9.7.0 | Server | MySQL database driver |
| python-dotenv | ≥ 1.2.2 | Server | Loads .env environment variables at startup |
| gunicorn | ≥ 26.0.0 | Server | WSGI server for production deployments |
| apscheduler | ≥ 3.11.0 | Server | Schedules periodic blockchain anchoring (every 5 min) |
| web3 | ≥ 7.16.0 | Server | Ethereum / Sepolia interaction for message anchoring |
| cryptography | ≥ 44.0.0 | crypto-library | X25519 key generation; PyCA-audited standard library |
| argon2-cffi | ≥ 25.1.0 | crypto-library | Argon2id password hashing and KEK derivation |
| requests | ≥ 2.32.0 | crypto-library | HTTP client used by the key publisher |
| pytest | any | Server, crypto-library | Test runner |
| Node.js | 18+ | Web client | Required to run npm install |
| argon2-browser | ≥ 1.18.0 | Web client | Argon2id in the browser for private key wrapping |
| Browser | Chrome 120+ / Firefox 121+ / Edge 120+ | Web client | Web Crypto API support (X25519, AES-GCM, HKDF) |
| CMake | ≥ 3.16 | C++ client | Build system |
| C++17 compiler | GCC 11+ via MSYS2 UCRT64 (Windows only) | C++ client | Compiles the C++ client |
| OpenSSL | 1.1.1+ | C++ client | TLS for HTTPS; AES-256-GCM encryption |
| libsodium | 1.0.18+ | C++ client | X25519 key exchange, HKDF-SHA-256, Argon2id |
| nlohmann/json | 3.11.3 | C++ client | JSON parsing (fetched automatically by CMake) |

---

## Running the Services

### Server

Create a `.env` file at the repo root:

```
# Required — server will not start without these
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=whatsas
DB_PASSWORD=<your-db-password>
DB_NAME=whatsas
JWT_SECRET_KEY=<long-random-secret>

# TLS — paths to the certificate and private key
SSL_CERT=/home/student/server.crt
SSL_KEY=/home/student/server.key

# Optional — enables blockchain anchoring; server starts without these but
# messages will not be anchored and the /flush endpoint will return 503
WEB3_RPC_URL=https://sepolia.infura.io/v3/<your-key>
CONTRACT_ADDRESS=0x<deployed-contract-address>
WALLET_PRIVATE_KEY=<hex-private-key-no-0x-prefix>
```

Install dependencies and start:

```bash
pip install -r requirements.txt
cd server && python run.py
```

For production, run under Gunicorn behind an nginx TLS proxy:

```bash
gunicorn -w 4 -b 127.0.0.1:5000 "app:create_app()"
```

Live deployment: https://sas.theburkenator.com

---

### Web Client

```bash
cd client-web && npm install
```

No build step needed. The Flask server serves the static files — once the server is running, visit https://sas.theburkenator.com (or https://localhost:5000 for local development).

The blockchain verification page is accessible without logging in at https://sas.theburkenator.com/#verify.

---

### C++ Client

> **Platform: Windows only.** The networking layer uses Winsock2 and the Windows CA store. It will not compile on Linux or macOS without a porting effort.

**Prerequisites — MSYS2 UCRT64**

Install MSYS2 from https://www.msys2.org, open the **UCRT64** shell (not MinGW64, not MSYS), and run:

```bash
pacman -Syu
```

Then close and reopen the UCRT64 shell and install dependencies:

```bash
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-libsodium \
          mingw-w64-ucrt-x86_64-nlohmann-json
```

Build commands can be run from **PowerShell** or the **UCRT64 shell** — both work as long as MSYS2 is installed.

**Server certificate**

The server uses a self-signed TLS certificate. Place it at `client-cpp/certs/server.crt` before running (`certs/` is gitignored). Copy it from the VM:

```bash
scp -P 2200 student@sas.theburkenator.com:/path/to/server.crt client-cpp/certs/server.crt
```

**Build** — run from the `client-cpp/` directory in PowerShell:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Run** — must be run from the `client-cpp/` directory:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
.\build\sas-client.exe
```

The UTF-8 line is required for emoji and Unicode to display correctly.

**Tests**

```powershell
ctest --test-dir build                  # all offline tests
ctest --test-dir build -L network       # include network tests (requires internet)
```

| Binary | What it tests |
|---|---|
| `test_e2e` | Full register → login → send → receive chain |
| `test_hpke` | DHKEM(X25519) + HKDF-SHA256 key derivation |
| `test_tamper` | AES-256-GCM tamper detection |
| `test_client` | Client crypto unit tests |
| `test_conversation` | Conversation deduplication and ordering |
| `test_tcp` | Raw TCP socket tests |
| `test_http` | HTTPS/TLS tests *(internet required)* |

---

## Smart Contract

The `DataStore` contract stores message hashes on-chain and emits a `DataStored` event for each anchored batch.

- **Network:** Ethereum Sepolia testnet
- **Source:** `contracts/DataStore.sol`
- **ABI:** `contracts/abi.json`
- **Deployed address:** set via `CONTRACT_ADDRESS` in `.env`

| Function | Description |
|---|---|
| `storeData(bytes32 dataHash)` | Stores a hash on-chain; returns the record ID |
| `getRecord(uint256 recordId)` | Returns (hash, timestamp, recorder) for a given record |
| `verifyData(uint256 recordId, bytes32 dataHash)` | Returns true if the stored hash matches |
| `recordCount()` | Total number of records stored |

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Blank screen on load | Open DevTools (F12) and check for JS errors; confirm the server is running |
| Messages show as (encrypted) | Check `sender_x25519_public_key` is present in the `/messages` response |
| argon2-browser not found | Run `npm install` inside `client-web/` |
| CORS errors on verify page | Ensure the Sepolia RPC URL is CORS-enabled for browser requests |
