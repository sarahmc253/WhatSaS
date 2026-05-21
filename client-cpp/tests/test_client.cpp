#include "../include/Client.hpp"
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

static std::string b64Encode(const unsigned char* data, std::size_t len) {
    std::size_t bufLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(bufLen, '\0');
    sodium_bin2base64(&out[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

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
    const std::string ad = "{\"sender_id\":\"alice\",\"recipient_id\":\"bob\","
                            "\"message_id\":\"deadbeef\",\"timestamp\":1234567890}";

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

static void testUniqueNonces() {
    // CSPRNG uniqueness is independent of AES-NI — test unconditionally.
    printf("\nTest 3: CSPRNG produces unique nonces\n");
    unsigned char n1[crypto_aead_aes256gcm_NPUBBYTES];
    unsigned char n2[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(n1, sizeof(n1));
    randombytes_buf(n2, sizeof(n2));
    check("two 12-byte nonces differ", std::memcmp(n1, n2, sizeof(n1)) != 0);
    check("base64 representations differ", b64Encode(n1, sizeof(n1)) != b64Encode(n2, sizeof(n2)));
}

static void testAdTamperFails() {
    printf("\nTest 4: AD tampering causes decryption failure\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    unsigned char key[crypto_aead_aes256gcm_KEYBYTES];
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(key, sizeof(key));
    randombytes_buf(nonce, sizeof(nonce));

    const std::string pt         = "secret payload";
    const std::string correctAd  = "{\"sender_id\":\"alice\",\"recipient_id\":\"bob\","
                                    "\"message_id\":\"aabbccdd\",\"timestamp\":100}";
    const std::string tamperedAd = "{\"sender_id\":\"mallory\",\"recipient_id\":\"bob\","
                                    "\"message_id\":\"aabbccdd\",\"timestamp\":100}";

    std::vector<uint8_t> ct(pt.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
        reinterpret_cast<const unsigned char*>(correctAd.data()), correctAd.size(),
        nullptr, nonce, key);
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
    // ceil(12/3)*4 = 16
    check("12 bytes encodes to 16 base64 chars", enc.size() == 16);
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
    testUniqueNonces();
    testAdTamperFails();
    testBase64Length();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
