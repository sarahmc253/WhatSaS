#include "../include/Client.hpp"
#include "../src/crypto_utils.hpp"
#include <sodium.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

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

static void testEncryptDecryptRoundTrip() {
    printf("\nTest 2: encryptAes256Gcm / decryptAes256Gcm round-trip\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES);
    randombytes_buf(key.data(), key.size());

    const std::string pt = "Hello, WhatSaS!";
    const std::string ad = buildAd("alice", "bob", "deadbeef", 1234567890);

    std::vector<uint8_t> blob = encryptAes256Gcm(key, pt, ad);
    check("encrypt returns non-empty blob", !blob.empty());
    check("blob is nonce(12) + pt + tag(16)",
          blob.size() == crypto_aead_aes256gcm_NPUBBYTES + pt.size() + crypto_aead_aes256gcm_ABYTES);

    auto recovered = decryptAes256Gcm(key, blob, ad);
    check("decrypt returns non-empty result", recovered.has_value() && !recovered->empty());
    check("recovered plaintext matches",
          recovered.has_value() &&
          std::string(reinterpret_cast<const char*>(recovered->data()), recovered->size()) == pt);
}

static void testWrongKeyFails() {
    printf("\nTest 3: Decryption with wrong key returns empty\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> keyA(crypto_aead_aes256gcm_KEYBYTES, 0xAA);
    std::vector<uint8_t> keyB(crypto_aead_aes256gcm_KEYBYTES, 0xBB);
    const std::string ad = buildAd("alice", "bob", "id1", 0);

    std::vector<uint8_t> blob = encryptAes256Gcm(keyA, "secret", ad);
    check("encrypt with keyA succeeds", !blob.empty());

    auto result = decryptAes256Gcm(keyB, blob, ad);
    check("decrypt with keyB returns empty", !result.has_value());
}

static void testAdTamperFails() {
    printf("\nTest 4: AD tampering causes decryption failure\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES);
    randombytes_buf(key.data(), key.size());

    const std::string correctAd  = buildAd("alice",   "bob", "aabbccdd", 100);
    const std::string tamperedAd = buildAd("mallory", "bob", "aabbccdd", 100);

    std::vector<uint8_t> blob = encryptAes256Gcm(key, "secret payload", correctAd);
    check("encrypt succeeds (precondition)", !blob.empty());
    if (blob.empty()) return;

    auto result = decryptAes256Gcm(key, blob, tamperedAd);
    check("tampered AD causes authentication failure", !result.has_value());
}

static void testCsprngNoncesAreUnique() {
    // Each encryptAes256Gcm call must draw a fresh CSPRNG nonce.
    // Extract the first 12 bytes of each blob and verify they differ.
    printf("\nTest 5: CSPRNG nonces are unique across encrypt calls\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x42);
    const std::string ad = buildAd("alice", "bob", "id", 0);

    std::vector<uint8_t> blob1 = encryptAes256Gcm(key, "msg1", ad);
    std::vector<uint8_t> blob2 = encryptAes256Gcm(key, "msg2", ad);
    check("both blobs non-empty", !blob1.empty() && !blob2.empty());

    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
    bool nonceDiffers = false;
    for (std::size_t i = 0; i < nonceLen; ++i) {
        if (blob1[i] != blob2[i]) { nonceDiffers = true; break; }
    }
    check("nonces differ between two encrypt calls (CSPRNG)", nonceDiffers);
}

static void testShortBlobRejected() {
    printf("\nTest 6: decryptAes256Gcm rejects blobs too short to hold a tag\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x01);
    // nonce(12) + tag(16) = 28 minimum; supply only 27 bytes
    std::vector<uint8_t> tooShort(27, 0x00);
    auto result = decryptAes256Gcm(key, tooShort, "ad");
    check("too-short blob returns empty", !result.has_value());
}

static void testBase64Length() {
    printf("\nTest 7: base64 encoding of 12-byte nonce\n");
    unsigned char nonce[12];
    randombytes_buf(nonce, sizeof(nonce));
    std::string enc = b64Encode(nonce, sizeof(nonce));
    check("12 bytes encodes to 16 base64 chars", enc.size() == 16);  // ceil(12/3)*4
}

static void testGenerateMsgId() {
    printf("\nTest 8: generateMsgId format and uniqueness\n");
    std::string id1 = generateMsgId();
    std::string id2 = generateMsgId();
    check("msg ID is 32 chars", id1.size() == 32);
    check("msg ID is lowercase hex",
          id1.find_first_not_of("0123456789abcdef") == std::string::npos);
    check("two msg IDs are distinct", id1 != id2);
}

static void testBuildAd() {
    printf("\nTest 9: buildAd canonical JSON structure\n");
    std::string ad = buildAd("alice", "bob", "cafebabe", 9999);
    check("contains sender_id",    ad.find("\"sender_id\":\"alice\"")     != std::string::npos);
    check("contains recipient_id", ad.find("\"recipient_id\":\"bob\"")    != std::string::npos);
    check("contains message_id",   ad.find("\"message_id\":\"cafebabe\"") != std::string::npos);
    check("contains timestamp",    ad.find("\"timestamp\":9999")          != std::string::npos);
    check("no spaces",             ad.find(' ')                           == std::string::npos);

    // Verify key order is deterministic (ordered_json insertion order preserved)
    auto senderPos    = ad.find("sender_id");
    auto recipientPos = ad.find("recipient_id");
    auto msgIdPos     = ad.find("message_id");
    auto tsPos        = ad.find("timestamp");
    check("keys in canonical order: sender < recipient < message_id < timestamp",
          senderPos < recipientPos && recipientPos < msgIdPos && msgIdPos < tsPos);

    std::string adEvil = buildAd("a\"b\\c", "bob", "id", 1);
    check("quotes in sender_id are escaped",
          adEvil.find("\"sender_id\":\"a\\\"b\\\\c\"") != std::string::npos);
}

static void testCiphertextTamperFails() {
    printf("\nTest 10: Tampered ciphertext is rejected at receiver\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES);
    randombytes_buf(key.data(), key.size());

    const std::string ad = buildAd("alice", "bob", "msg001", 1000000);
    const std::string pt = "tamper me if you dare";

    std::vector<uint8_t> blob = encryptAes256Gcm(key, pt, ad);
    check("encrypt succeeds (precondition)", !blob.empty());

    constexpr std::size_t minLen =
        crypto_aead_aes256gcm_NPUBBYTES + 1 + crypto_aead_aes256gcm_ABYTES;
    if (blob.size() < minLen) {
        printf("[FAIL] blob too short to contain ciphertext byte — skipping tamper\n");
        ++failed;
        return;
    }

    // Flip a byte in the ciphertext body (after the 12-byte nonce, before the 16-byte tag).
    std::vector<uint8_t> tampered = blob;
    tampered[crypto_aead_aes256gcm_NPUBBYTES] ^= 0xFF;

    auto result = decryptAes256Gcm(key, tampered, ad);
    check("tampered ciphertext rejected by AEAD tag verification", !result.has_value());
}

// ============================================================================

int main() {
    if (sodium_init() < 0) {
        printf("FATAL: libsodium init failed\n");
        return 1;
    }
    printf("=== Client crypto tests (offline) ===\n");

    testConstructorKeyValidation();
    testEncryptDecryptRoundTrip();
    testWrongKeyFails();
    testAdTamperFails();
    testCsprngNoncesAreUnique();
    testShortBlobRejected();
    testBase64Length();
    testGenerateMsgId();
    testBuildAd();
    testCiphertextTamperFails();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
