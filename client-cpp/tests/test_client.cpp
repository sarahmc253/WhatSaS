#include "../include/Client.hpp"
#include "../src/crypto_utils.hpp"
#include <sodium.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool ok, const char* detail = "") {
    if (ok) { printf("[PASS] %s\n", label); ++passed; }
    else     { printf("[FAIL] %s %s\n", label, detail); ++failed; }
}

static bool aesAvailable() { return crypto_aead_aes256gcm_is_available() != 0; }

// ============================================================================

static void testConstructorKeyValidation() {
    printf("\nTest 1: Constructor rejects wrong-size keys\n");
    for (std::size_t sz : {0u, 16u, 31u, 33u, 64u}) {
        std::vector<uint8_t> key(sz, 0xAB);
        bool threw = false;
        try { Client c("https://example.com", "alice", key); }
        catch (const std::invalid_argument&) { threw = true; }
        catch (...) {}
        char label[64];
        std::snprintf(label, sizeof(label), "rejects %zu-byte key", sz);
        check(label, threw);
    }
}

static void testAesRoundTrip() {
    printf("\nTest 2: AES-256-GCM encrypt/decrypt round-trip\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    unsigned char key[crypto_aead_aes256gcm_KEYBYTES];
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(key, sizeof(key));
    randombytes_buf(nonce, sizeof(nonce));

    const std::string pt = "Hello, WhatSaS!";
    const std::string ad = buildAd("alice", "bob", "deadbeef", 1234567890);

    std::vector<uint8_t> ct(pt.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nullptr, nonce, key);
    check("encrypt succeeds", rc == 0);
    ct.resize(ctLen);
    check("ciphertext = plaintext + 16-byte tag",
          ct.size() == pt.size() + crypto_aead_aes256gcm_ABYTES);

    std::vector<uint8_t> recovered(ct.size());
    unsigned long long recovLen = 0;
    int rc2 = crypto_aead_aes256gcm_decrypt(
        recovered.data(), &recovLen, nullptr,
        ct.data(), ct.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nonce, key);
    check("decrypt succeeds", rc2 == 0);
    recovered.resize(recovLen);
    check("recovered plaintext matches",
          std::string(reinterpret_cast<const char*>(recovered.data()), recovLen) == pt);
}

static void testCounterNoncesAreUnique() {
    // Counter-derived nonces: same base, incrementing count — must differ each call.
    // This is independent of AES-NI; test unconditionally.
    printf("\nTest 3: Counter-based nonces are unique\n");

    unsigned char base[12];
    randombytes_buf(base, sizeof(base));

    auto deriveNonce = [&](uint64_t count) {
        unsigned char n[12];
        std::memcpy(n, base, 12);
        for (int i = 0; i < 8; ++i)
            n[i] ^= static_cast<unsigned char>((count >> (8 * i)) & 0xFF);
        return std::string(reinterpret_cast<const char*>(n), 12);
    };

    std::string n0 = deriveNonce(0);
    std::string n1 = deriveNonce(1);
    std::string n2 = deriveNonce(2);
    check("counter 0 != counter 1", n0 != n1);
    check("counter 1 != counter 2", n1 != n2);
    check("counter 0 != counter 2", n0 != n2);
    // Rollover: same count always produces same nonce
    check("same counter reproduces same nonce", deriveNonce(1) == n1);
}

static void testAdTamperFails() {
    printf("\nTest 4: AD tampering causes decryption failure\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    unsigned char key[crypto_aead_aes256gcm_KEYBYTES];
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(key, sizeof(key));
    randombytes_buf(nonce, sizeof(nonce));

    const std::string pt         = "secret payload";
    const std::string correctAd  = buildAd("alice",   "bob", "aabbccdd", 100);
    const std::string tamperedAd = buildAd("mallory", "bob", "aabbccdd", 100);

    std::vector<uint8_t> ct(pt.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    // Assert encryption succeeds before testing tamper — a silent encrypt failure
    // would produce empty ciphertext, causing decrypt to trivially return -1 and
    // the test to pass vacuously without exercising AEAD at all.
    int encRc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
        reinterpret_cast<const unsigned char*>(correctAd.data()), correctAd.size(),
        nullptr, nonce, key);
    check("encrypt succeeds (precondition)", encRc == 0);
    check("ciphertext is non-empty (precondition)", ctLen > 0);
    if (encRc != 0 || ctLen == 0) return;
    ct.resize(ctLen);

    std::vector<uint8_t> out(ct.size());
    unsigned long long outLen = 0;
    int rc = crypto_aead_aes256gcm_decrypt(
        out.data(), &outLen, nullptr,
        ct.data(), ct.size(),
        reinterpret_cast<const unsigned char*>(tamperedAd.data()), tamperedAd.size(),
        nonce, key);
    check("tampered AD causes authentication failure", rc == -1);
}

static void testBase64Length() {
    printf("\nTest 5: base64 encoding of 12-byte nonce\n");
    unsigned char nonce[12];
    randombytes_buf(nonce, sizeof(nonce));
    std::string enc = b64Encode(nonce, sizeof(nonce));
    check("12 bytes encodes to 16 base64 chars", enc.size() == 16);  // ceil(12/3)*4
}

static void testGenerateMsgId() {
    printf("\nTest 6: generateMsgId format and uniqueness\n");
    std::string id1 = generateMsgId();
    std::string id2 = generateMsgId();
    check("msg ID is 32 chars", id1.size() == 32);
    check("msg ID is lowercase hex",
          id1.find_first_not_of("0123456789abcdef") == std::string::npos);
    check("two msg IDs are distinct", id1 != id2);
}

static void testBuildAd() {
    printf("\nTest 7: buildAd canonical JSON structure\n");
    std::string ad = buildAd("alice", "bob", "cafebabe", 9999);
    check("contains sender_id",    ad.find("\"sender_id\":\"alice\"")     != std::string::npos);
    check("contains recipient_id", ad.find("\"recipient_id\":\"bob\"")    != std::string::npos);
    check("contains message_id",   ad.find("\"message_id\":\"cafebabe\"") != std::string::npos);
    check("contains timestamp",    ad.find("\"timestamp\":9999")          != std::string::npos);
    check("no spaces",             ad.find(' ')                           == std::string::npos);

    // Special chars in sender_id must be escaped, not break JSON structure
    std::string adEvil = buildAd("a\"b\\c", "bob", "id", 1);
    check("quotes in sender_id are escaped",
          adEvil.find("\"sender_id\":\"a\\\"b\\\\c\"") != std::string::npos);
}

// ============================================================================

int main() {
    if (sodium_init() < 0) {
        printf("FATAL: libsodium init failed\n");
        return 1;
    }
    printf("=== Client crypto tests (offline) ===\n");

    testConstructorKeyValidation();
    testAesRoundTrip();
    testCounterNoncesAreUnique();
    testAdTamperFails();
    testBase64Length();
    testGenerateMsgId();
    testBuildAd();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
