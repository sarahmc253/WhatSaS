#define NOMINMAX
#include "../src/crypto_utils.hpp"
#include <sodium.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool ok, const char* detail = "") {
    if (ok) { printf("[PASS] %s\n", label); ++passed; }
    else     { printf("[FAIL] %s %s\n", label, detail); ++failed; }
}

static void testTamperFirstByte() {
    printf("\nTest 1: flip first ciphertext byte\n");

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x42);
    const std::string plaintext = "hello whatsas";
    const std::string ad = buildAd("alice", "bob", "msg-001", 1000000);

    std::vector<uint8_t> blob = encryptAes256Gcm(key, plaintext, ad);
    check("encrypt produces non-empty blob", !blob.empty());
    if (blob.empty()) return;

    // blob layout: nonce (12 bytes) || ciphertext+tag
    // Flip the first byte of the ciphertext portion so the AEAD tag
    // no longer matches and decryption must return nullopt.
    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
    check("blob large enough to tamper", blob.size() > nonceLen);
    if (blob.size() <= nonceLen) return;

    const auto baseline = decryptAes256Gcm(key, blob, ad);
    check("baseline decryption succeeds", baseline.has_value());
    if (!baseline.has_value()) return;
    const std::string baselineText(baseline->begin(), baseline->end());
    check("baseline plaintext matches original", baselineText == plaintext);
    if (baselineText != plaintext) return;

    blob[nonceLen] ^= 0xFF;

    const auto result = decryptAes256Gcm(key, blob, ad);
    check("tampered first byte rejected by AEAD tag", !result.has_value());
}

static void testTamperMiddleByte() {
    printf("\nTest 2: flip middle ciphertext byte\n");

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x42);
    const std::string plaintext = "hello whatsas";
    const std::string ad = buildAd("alice", "bob", "msg-001", 1000000);

    std::vector<uint8_t> blob = encryptAes256Gcm(key, plaintext, ad);
    check("encrypt produces non-empty blob", !blob.empty());
    if (blob.empty()) return;

    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
    const std::size_t ctLen = blob.size() - nonceLen;
    check("ciphertext long enough to have a middle byte", ctLen >= 2);
    if (ctLen < 2) return;

    const auto baseline = decryptAes256Gcm(key, blob, ad);
    check("baseline decryption succeeds", baseline.has_value());
    if (!baseline.has_value()) return;
    const std::string baselineText(baseline->begin(), baseline->end());
    check("baseline plaintext matches original", baselineText == plaintext);
    if (baselineText != plaintext) return;

    // Flip a byte in the middle of the ciphertext portion.
    const std::size_t midIdx = nonceLen + ctLen / 2;
    blob[midIdx] ^= 0xFF;

    const auto result = decryptAes256Gcm(key, blob, ad);
    check("tampered middle byte rejected by AEAD tag", !result.has_value());
}

int main() {
    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium init failed\n");
        return 1;
    }
    if (!crypto_aead_aes256gcm_is_available()) {
        printf("[SKIP] AES-256-GCM unavailable (no hardware AES-NI)\n");
        return 0;
    }

    testTamperFirstByte();
    testTamperMiddleByte();

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
