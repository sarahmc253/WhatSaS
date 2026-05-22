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
