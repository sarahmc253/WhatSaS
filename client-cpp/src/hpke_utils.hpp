#ifndef HPKE_UTILS_HPP
#define HPKE_UTILS_HPP

// DHKEM(X25519, HKDF-SHA256) — HPKE Mode_Auth construction using libsodium only.
//
// Security properties:
//   Confidentiality  — DH1 = X25519(eph_sk, recipient_pk) ties key to recipient's static key.
//   Sender auth      — DH2 = X25519(sender_sk, recipient_pk) ties key to sender's identity.
//   Forward secrecy  — fresh ephemeral keypair per message; eph_sk is zeroized after use.
//   Server blindness — only eph_pk (kem_output) reaches the server; secret never transmitted.
//
// Trust model: TOFU with local pinning (handled in Client). First key fetch is pinned;
// subsequent fetches that diverge are rejected as potential key-substitution attacks.

#include "crypto_utils.hpp"
#include <sodium.h>
#include <optional>
#include <vector>
#include <algorithm>

static constexpr const char* HPKE_INFO = "WhatSaS-HPKE-v1";

struct HpkeKeypair {
    std::vector<uint8_t> pk;  // 32 bytes — X25519 public key; safe to publish
    std::vector<uint8_t> sk;  // 32 bytes — X25519 private key; never leaves client
};

struct HpkeSendResult {
    std::vector<uint8_t> aesKey;  // 32-byte derived AES-256 key — use once, then zeroize
    std::vector<uint8_t> ephPk;   // 32-byte ephemeral public key — send in kem_output field
};

// Returns true if all bytes of v are zero (low-order point check).
static inline bool isAllZero(const std::vector<uint8_t>& v) {
    return std::all_of(v.begin(), v.end(), [](uint8_t b){ return b == 0; });
}

// Generate a fresh X25519 keypair for a user's long-term identity.
static inline HpkeKeypair hpkeGenerateKeypair() {
    HpkeKeypair kp;
    kp.pk.resize(crypto_box_PUBLICKEYBYTES);   // 32 bytes
    kp.sk.resize(crypto_box_SECRETKEYBYTES);   // 32 bytes
    crypto_box_keypair(kp.pk.data(), kp.sk.data());
    return kp;
}

// DHKEM Send: derive a per-message AES-256 key for encrypting to recipientPk.
//
// senderSk    — local user's long-term X25519 private key (32 bytes).
// recipientPk — peer's registered X25519 public key (32 bytes).
//
// Returns nullopt if either key is wrong size, any DH produces a low-order output,
// or HKDF fails. On success, caller must zeroize aesKey after use.
static inline std::optional<HpkeSendResult> hpkeSend(
    const std::vector<uint8_t>& senderSk,
    const std::vector<uint8_t>& recipientPk)
{
    if (senderSk.size() != 32 || recipientPk.size() != 32) return std::nullopt;

    // 1. Fresh ephemeral keypair (CSPRNG via libsodium).
    unsigned char eph_pk[32], eph_sk[32];
    crypto_box_keypair(eph_pk, eph_sk);

    // 2. DH1 = X25519(eph_sk, recipient_pk) — freshness.
    unsigned char dh1[32];
    if (crypto_scalarmult(dh1, eph_sk, recipientPk.data()) != 0) {
        sodium_memzero(eph_sk, sizeof(eph_sk));
        return std::nullopt;
    }

    // 3. DH2 = X25519(sender_sk, recipient_pk) — sender authentication.
    unsigned char dh2[32];
    if (crypto_scalarmult(dh2, senderSk.data(), recipientPk.data()) != 0) {
        sodium_memzero(eph_sk, sizeof(eph_sk));
        sodium_memzero(dh1, sizeof(dh1));
        return std::nullopt;
    }

    // Zeroize eph_sk immediately — not needed after this point.
    sodium_memzero(eph_sk, sizeof(eph_sk));

    // 4. Low-order point check: a small-subgroup public key causes DH to output zero
    //    regardless of the scalar, leaking no information about the private key but
    //    producing a trivially predictable shared secret.
    std::vector<uint8_t> dh1v(dh1, dh1 + 32);
    std::vector<uint8_t> dh2v(dh2, dh2 + 32);
    if (isAllZero(dh1v) || isAllZero(dh2v)) {
        sodium_memzero(dh1, sizeof(dh1));
        sodium_memzero(dh2, sizeof(dh2));
        return std::nullopt;
    }

    // 5. IKM = DH1 || DH2 (64 bytes).
    std::vector<uint8_t> ikm;
    ikm.reserve(64);
    ikm.insert(ikm.end(), dh1, dh1 + 32);
    ikm.insert(ikm.end(), dh2, dh2 + 32);
    sodium_memzero(dh1, sizeof(dh1));
    sodium_memzero(dh2, sizeof(dh2));

    // 6. HKDF-Extract: PRK = HMAC-SHA256(salt=eph_pk, IKM).
    //    Using eph_pk as salt ties PRK to this specific message's ephemeral component.
    std::vector<uint8_t> salt(eph_pk, eph_pk + 32);
    std::vector<uint8_t> prk = hkdfExtract(salt, ikm);
    sodium_memzero(ikm.data(), ikm.size());
    if (prk.empty()) return std::nullopt;

    // 7. HKDF-Expand: AES_KEY = HMAC-SHA256(PRK, info || 0x01).
    std::vector<uint8_t> aesKey = hkdfExpand32(prk, HPKE_INFO);
    sodium_memzero(prk.data(), prk.size());
    if (aesKey.empty()) return std::nullopt;

    HpkeSendResult result;
    result.aesKey = std::move(aesKey);
    result.ephPk  = std::vector<uint8_t>(eph_pk, eph_pk + 32);
    return result;
}

// DHKEM Receive: re-derive the same AES-256 key from the message's ephemeral public key.
//
// recipientSk — local user's long-term X25519 private key (32 bytes).
// ephPk       — kem_output field from the received message (32 bytes, base64-decoded).
// senderPk    — sender's registered X25519 public key (32 bytes, fetched + TOFU-pinned).
//
// Returns empty vector on any failure (wrong key size, low-order point, HKDF error).
// On success, caller must zeroize the returned key after use.
static inline std::vector<uint8_t> hpkeReceive(
    const std::vector<uint8_t>& recipientSk,
    const std::vector<uint8_t>& ephPk,
    const std::vector<uint8_t>& senderPk)
{
    if (recipientSk.size() != 32 || ephPk.size() != 32 || senderPk.size() != 32)
        return {};

    // DH1 = X25519(recipient_sk, eph_pk)  —  symmetric to sender's DH1.
    unsigned char dh1[32];
    if (crypto_scalarmult(dh1, recipientSk.data(), ephPk.data()) != 0) return {};

    // DH2 = X25519(recipient_sk, sender_pk)  —  symmetric to sender's DH2.
    unsigned char dh2[32];
    if (crypto_scalarmult(dh2, recipientSk.data(), senderPk.data()) != 0) {
        sodium_memzero(dh1, sizeof(dh1));
        return {};
    }

    std::vector<uint8_t> dh1v(dh1, dh1 + 32);
    std::vector<uint8_t> dh2v(dh2, dh2 + 32);
    if (isAllZero(dh1v) || isAllZero(dh2v)) {
        sodium_memzero(dh1, sizeof(dh1));
        sodium_memzero(dh2, sizeof(dh2));
        return {};
    }

    std::vector<uint8_t> ikm;
    ikm.reserve(64);
    ikm.insert(ikm.end(), dh1, dh1 + 32);
    ikm.insert(ikm.end(), dh2, dh2 + 32);
    sodium_memzero(dh1, sizeof(dh1));
    sodium_memzero(dh2, sizeof(dh2));

    std::vector<uint8_t> prk = hkdfExtract(ephPk, ikm);
    sodium_memzero(ikm.data(), ikm.size());
    if (prk.empty()) return {};

    std::vector<uint8_t> aesKey = hkdfExpand32(prk, HPKE_INFO);
    sodium_memzero(prk.data(), prk.size());
    return aesKey;
}

#endif // HPKE_UTILS_HPP
