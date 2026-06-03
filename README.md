# WhatSaS

Secure messaging application — CS4455 Cybersecurity Epic 2026, Group sas.

**Team:** Sarah McDonagh, Sreejita Saha, Aoibheann Mangan

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

## Dependencies at a Glance

| Dependency | Version | Component | Purpose |
|---|---|---|---|
| **Python** | 3.10 + | Server, crypto-library | Runtime for the Flask API and crypto utilities |
| **MySQL** | 8.0 + | Server | Stores users, messages, audit log, and blockchain records |
| `flask` | ≥ 3.1.3 | Server | HTTP API framework |
| `flask-jwt-extended` | ≥ 4.7.4 | Server | JWT authentication middleware |
| `mysql-connector-python` | ≥ 9.7.0 | Server | MySQL database driver |
| `python-dotenv` | ≥ 1.2.2 | Server | Loads `.env` environment variables at startup |
| `gunicorn` | ≥ 26.0.0 | Server | WSGI server for production deployments |
| `apscheduler` | ≥ 3.11.0 | Server | Schedules periodic blockchain anchoring (every 5 min) |
| `web3` | ≥ 7.16.0 | Server | Ethereum / Sepolia interaction for message anchoring |
| `cryptography` | ≥ 44.0.0 | crypto-library | X25519 key generation; PyCA-audited standard library |
| `argon2-cffi` | ≥ 25.1.0 | crypto-library | Argon2id password hashing and KEK derivation |
| `requests` | ≥ 2.32.0 | crypto-library | HTTP client used by the key publisher |
| `pytest` | any | Server, crypto-library | Test runner |
| **Node.js** | 18 + | Web client | Required to run `npm install` |
| `argon2-browser` | ≥ 1.18.0 | Web client | Argon2id in the browser for private key wrapping |
| **Browser** | Chrome 120 + / Firefox 121 + / Edge 120 + | Web client | Web Crypto API support (X25519, AES-GCM, HKDF) |
| **CMake** | ≥ 3.16 | C++ client | Build system |
| **C++17 compiler** | GCC 11 + / Clang 14 + / MSVC 2019 + | C++ client | Compiles the C++ client |
| **OpenSSL** | 1.1.1 + | C++ client | TLS for HTTPS; AES-256-GCM encryption |
| **libsodium** | 1.0.18 + | C++ client | X25519 key exchange, HKDF-SHA-256, Argon2id |
| `nlohmann/json` | 3.11.3 | C++ client | JSON parsing (fetched automatically by CMake) |

---

## Running the Services

### Server

Create a `.env` file at the repo root (gitignored):

```dotenv
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

Live deployment: **https://sas.theburkenator.com**

---

### Web client

```bash
cd client-web && npm install
```

No build step needed. The Flask server serves the static files — once the server is running, visit **https://sas.theburkenator.com** (or `https://localhost:5000` for local development).

**Blockchain verification page** — accessible without logging in at `https://sas.theburkenator.com/#verify`. Requires this meta tag in `index.html` for Sepolia access:

```html
<meta name="sepolia-rpc-url" content="https://sepolia.infura.io/v3/<your-key>">
```

To run the verify page locally without the Flask backend:

```bash
cd client-web
python3 -m http.server 8080
```

Then open `http://localhost:8080/verify.html` in your browser.

**Troubleshooting**

| Problem | Fix |
|---|---|
| Blank screen on load | Open DevTools (`F12`) and check for JS errors; confirm the server is running |
| Messages show as `(encrypted)` | Check `sender_x25519_public_key` is present in the `/messages` response |
| `argon2-browser` not found | Run `npm install` inside `client-web/` |
| CORS errors on verify page | Ensure the Sepolia RPC URL in `index.html` is CORS-enabled for browser requests |

---

### C++ client

`nlohmann/json` is fetched automatically by CMake via `FetchContent` — no manual install needed.

**Ubuntu / Debian**
```bash
sudo apt install cmake ninja-build g++ libssl-dev libsodium-dev
```

**macOS**
```bash
brew install cmake ninja openssl libsodium
```

**Windows (MSYS2 UCRT64 shell)**
```bash
pacman -S mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-openssl \
          mingw-w64-ucrt-x86_64-libsodium
```

**Build**

Run from the `client-cpp/` directory:

```bash
cd client-cpp
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Server certificate**

The server uses a self-signed TLS certificate. Place it at `client-cpp/certs/server.crt` before running (`certs/` is gitignored). Obtain it from a team member or copy it directly from the VM:

```bash
scp -P 2200 student@sas.theburkenator.com:/path/to/server.crt client-cpp/certs/server.crt
```

**Run**

Must be run from the `client-cpp/` directory so `certs/server.crt` resolves correctly:

```bash
# Linux / macOS
./build/sas-client

# Windows (set UTF-8 output first)
$OutputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
.\build\sas-client.exe
```

**Tests**

```bash
ctest --test-dir build                  # all offline tests
ctest --test-dir build -L network       # include network tests (requires internet)
```

Individual targets: `test_e2e`, `test_hpke`, `test_tamper`, `test_client`, `test_conversation`, `test_tcp`, `test_http`.

---

### Smart contract

The `DataStore` contract stores SHA-256 message hashes on-chain and emits a `DataStored` event for each anchored batch.

- **Network:** Ethereum Sepolia testnet
- **Source:** [`contracts/DataStore.sol`](contracts/DataStore.sol)
- **ABI:** [`contracts/abi.json`](contracts/abi.json)
- **Deployed address:** set via `CONTRACT_ADDRESS` in `.env` (see server setup above)

**Contract interface**

| Function | Description |
|---|---|
| `storeData(bytes32 dataHash)` | Stores a hash on-chain; returns the record ID |
| `getRecord(uint256 recordId)` | Returns `(hash, timestamp, recorder)` for a given record |
| `verifyData(uint256 recordId, bytes32 dataHash)` | Returns `true` if the stored hash matches |
| `recordCount()` | Total number of records stored |

---

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
