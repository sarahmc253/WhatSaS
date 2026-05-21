#include "../include/Client.hpp"
#include <sodium.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

// Test harness

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool condition, const char* detail = "") {
    if (condition) {
        printf("[PASS] %s\n", label);
        ++passed;
    } else {
        printf("[FAIL] %s %s\n", label, detail);
        ++failed;
    }
}

// Helpers mirroring Client.cpp internals (tested independently of HTTP)

static std::string b64Encode(const unsigned char* data, std::size_t len) {
    std::size_t bufLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(bufLen, '\0');
    sodium_bin2base64(&out[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Test 1: Constructor rejects wrong-size key

static void testConstructorRejectsShortKey() {
    printf("\nTest 1: Constructor rejects key < 32 bytes\n");
    std::vector<uint8_t> shortKey(16, 0xAB);
    bool threw = false;
    try {
        Client c("https://example.com", "alice", shortKey);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {}
    check("invalid_argument thrown for 16-byte key", threw);
}

static void testConstructorRejectsLongKey() {
    printf("\nTest 2: Constructor rejects key > 32 bytes\n");
    std::vector<uint8_t> longKey(64, 0xAB);
    bool threw = false;
    try {
        Client c("https://example.com", "alice", longKey);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {}
    check("invalid_argument thrown for 64-byte key", threw);
}

// Test 3: AES-256-GCM round-trip (encrypt + decrypt via raw libsodium)

static void testAesRoundTrip() {
    printf("\nTest 3: AES-256-GCM encrypt/decrypt round-trip\n");

    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI not available on this CPU — skipping crypto tests\n");
        return;
    }

    // Use a known 32-byte key (test only — real keys come from HKDF)
    unsigned char key[crypto_aead_aes256gcm_KEYBYTES];
    randombytes_buf(key, sizeof(key));

    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    const std::string plaintext = "Hello, WhatSaS!";
    const std::string ad        = "{\"sender_id\":\"alice\",\"recipient_id\":\"bob\","
                                   "\"message_id\":\"deadbeef\",\"timestamp\":1234567890}";

    // Encrypt
    std::vector<uint8_t> ct(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    int rc = crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nullptr, nonce, key);
    check("encrypt returns 0", rc == 0);
    ct.resize(ctLen);
    check("ciphertext length = plaintext + 16-byte tag",
          ct.size() == plaintext.size() + crypto_aead_aes256gcm_ABYTES);

    // Decrypt
    std::vector<uint8_t> recovered(ct.size());
    unsigned long long recovLen = 0;
    int rc2 = crypto_aead_aes256gcm_decrypt(
        recovered.data(), &recovLen,
        nullptr,
        ct.data(), ct.size(),
        reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
        nonce, key);
    check("decrypt returns 0", rc2 == 0);
    recovered.resize(recovLen);

    std::string recoveredStr(reinterpret_cast<const char*>(recovered.data()), recovLen);
    check("decrypted plaintext matches original", recoveredStr == plaintext);
}

// Test 4: CSPRNG produces unique nonces

static void testUniquNonces() {
    printf("\nTest 4: CSPRNG generates unique nonces\n");

    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI not available — skipping\n");
        return;
    }

    unsigned char n1[crypto_aead_aes256gcm_NPUBBYTES];
    unsigned char n2[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(n1, sizeof(n1));
    randombytes_buf(n2, sizeof(n2));

    // Two CSPRNG nonces must differ with overwhelming probability (2^-96 collision chance)
    bool differ = (std::memcmp(n1, n2, sizeof(n1)) != 0);
    check("two CSPRNG nonces are distinct", differ);

    std::string b641 = b64Encode(n1, sizeof(n1));
    std::string b642 = b64Encode(n2, sizeof(n2));
    check("base64-encoded nonces are distinct", b641 != b642);
}

// Test 5: AD tampering causes decryption failure

static void testAdTamperFails() {
    printf("\nTest 5: AD tampering causes decryption failure (AEAD integrity)\n");

    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI not available — skipping\n");
        return;
    }

    unsigned char key[crypto_aead_aes256gcm_KEYBYTES];
    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(key, sizeof(key));
    randombytes_buf(nonce, sizeof(nonce));

    const std::string plaintext  = "secret payload";
    const std::string correctAd  = "{\"sender_id\":\"alice\",\"recipient_id\":\"bob\","
                                    "\"message_id\":\"aabbccdd\",\"timestamp\":100}";
    const std::string tamperedAd = "{\"sender_id\":\"mallory\",\"recipient_id\":\"bob\","
                                    "\"message_id\":\"aabbccdd\",\"timestamp\":100}";

    std::vector<uint8_t> ct(plaintext.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ctLen = 0;
    crypto_aead_aes256gcm_encrypt(
        ct.data(), &ctLen,
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        reinterpret_cast<const unsigned char*>(correctAd.data()), correctAd.size(),
        nullptr, nonce, key);
    ct.resize(ctLen);

    // Decrypt with tampered AD — must fail (returns -1)
    std::vector<uint8_t> out(ct.size());
    unsigned long long outLen = 0;
    int rc = crypto_aead_aes256gcm_decrypt(
        out.data(), &outLen,
        nullptr,
        ct.data(), ct.size(),
        reinterpret_cast<const unsigned char*>(tamperedAd.data()), tamperedAd.size(),
        nonce, key);
    check("decrypt with tampered AD returns -1 (authentication failure)", rc == -1);
}

// Test 6: base64 encoding is non-empty and correct length

static void testBase64Encoding() {
    printf("\nTest 6: base64 encoding of 12-byte nonce\n");

    unsigned char nonce[12];
    randombytes_buf(nonce, sizeof(nonce));
    std::string encoded = b64Encode(nonce, sizeof(nonce));

    // Standard base64: ceil(12/3)*4 = 16 characters
    check("12-byte nonce encodes to 16 base64 chars", encoded.size() == 16);
    check("base64 output is non-empty", !encoded.empty());
}

// Entry point

int main() {
    if (sodium_init() < 0) {
        printf("FATAL: libsodium initialisation failed\n");
        return 1;
    }

    printf("=== Client AES-256-GCM Unit Tests ===\n");
    printf("(offline — no network required)\n");

    testConstructorRejectsShortKey();
    testConstructorRejectsLongKey();
    testAesRoundTrip();
    testUniquNonces();
    testAdTamperFails();
    testBase64Encoding();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
