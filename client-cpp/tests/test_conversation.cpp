#include "../include/Client.hpp"
#include "../include/Conversation.hpp"
#include "../include/Message.hpp"
#include "../src/crypto_utils.hpp"
#include "../src/message_crypto.hpp"

#include <sodium.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool ok, const char* detail = "") {
    if (ok) { printf("[PASS] %s\n", label); ++passed; }
    else     { printf("[FAIL] %s %s\n", label, detail); ++failed; }
}

static bool aesAvailable() { return crypto_aead_aes256gcm_is_available() != 0; }

// Helper: make a valid 32-char hex message ID string
static std::string makeHexId(unsigned char byte_val) {
    unsigned char raw[16];
    std::memset(raw, byte_val, sizeof(raw));
    std::string hex(33, '\0');
    sodium_bin2hex(&hex[0], 33, raw, sizeof(raw));
    hex.resize(32);
    return hex;
}

// Helper: encrypt a plaintext and return a Message with separate nonce/ciphertext
static Message makeEncryptedMessage(
    const std::vector<uint8_t>& key,
    const std::string& senderId,
    const std::string& recipientId,
    const std::string& plaintext,
    std::time_t ts)
{
    const std::string msgId = makeHexId(0xAB);
    const std::string ad    = buildAd(senderId, recipientId, msgId, ts);
    std::vector<uint8_t> blob = encryptAes256Gcm(key, plaintext, ad);

    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
    std::vector<uint8_t> nonce(blob.begin(), blob.begin() + nonceLen);
    std::vector<uint8_t> ct(blob.begin() + nonceLen, blob.end());
    return Message(msgId, senderId, recipientId, ct, nonce, ts);
}

// ============================================================================

static void testEncryptDecryptRoundTrip() {
    printf("\nTest 1: encryptMessage + decryptMessage round-trip\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES);
    randombytes_buf(key.data(), key.size());

    auto enc = encryptMessage(key, "alice", "bob", "Hello, WhatSaS!");
    check("encryptMessage returns non-empty optional", enc.has_value());
    if (!enc) return;

    check("messageId is 32 chars", enc->messageId.size() == 32);
    check("nonceB64 non-empty",    !enc->nonceB64.empty());
    check("ctB64 non-empty",       !enc->ctB64.empty());

    std::vector<uint8_t> nonce = b64Decode(enc->nonceB64);
    std::vector<uint8_t> ct    = b64Decode(enc->ctB64);
    Message msg(enc->messageId, "alice", "bob", ct, nonce, enc->timestamp);

    auto dm = decryptMessage(key, msg);
    check("decryptMessage returns non-empty optional", dm.has_value());
    if (!dm) return;
    check("plaintext round-trips correctly", dm->plaintext == "Hello, WhatSaS!");
    check("senderId preserved",   dm->senderId    == "alice");
    check("recipientId preserved",dm->recipientId == "bob");
    check("messageId preserved",  dm->messageId   == enc->messageId);
}

static void testWrongKeyFails() {
    printf("\nTest 2: decryptMessage with wrong key returns nullopt\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> keyA(crypto_aead_aes256gcm_KEYBYTES, 0xAA);
    std::vector<uint8_t> keyB(crypto_aead_aes256gcm_KEYBYTES, 0xBB);

    Message msg = makeEncryptedMessage(keyA, "alice", "bob", "secret", 100);
    auto dm = decryptMessage(keyB, msg);
    check("wrong key returns nullopt", !dm.has_value());
}

static void testTamperedCiphertextFails() {
    printf("\nTest 3: decryptMessage with tampered ciphertext returns nullopt\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x42);
    Message orig = makeEncryptedMessage(key, "alice", "bob", "secret payload", 200);

    // Flip one byte in ciphertext
    std::vector<uint8_t> tamperedCt = orig.getCiphertext();
    tamperedCt[0] ^= 0xFF;
    Message tampered(orig.getMessageId(), orig.getSenderId(), orig.getRecipientId(),
                     tamperedCt, orig.getNonce(), orig.getTimestamp());

    auto dm = decryptMessage(key, tampered);
    check("tampered ciphertext returns nullopt", !dm.has_value());
}

static void testMessageConstructorRejectsShortNonce() {
    printf("\nTest 4: Message constructor throws on nonce != 12 bytes\n");
    std::vector<uint8_t> nonce(11, 0x00);  // wrong size
    std::vector<uint8_t> ct(32, 0x00);
    bool threw = false;
    try {
        Message m(makeHexId(0x01), "a", "b", ct, nonce, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {}
    check("throws on 11-byte nonce", threw);
}

static void testMessageConstructorRejectsShortCiphertext() {
    printf("\nTest 5: Message constructor throws on ciphertext < 16 bytes\n");
    std::vector<uint8_t> nonce(12, 0x00);
    std::vector<uint8_t> ct(15, 0x00);  // too short (must be >= 16)
    bool threw = false;
    try {
        Message m(makeHexId(0x02), "a", "b", ct, nonce, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    } catch (...) {}
    check("throws on 15-byte ciphertext", threw);
}

static void testConversationSortOrder() {
    printf("\nTest 6: Conversation::getMessages returns timestamp-sorted order\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x55);
    Conversation conv("alice");

    // Insert in order 300, 100, 200
    for (std::time_t ts : {300, 100, 200}) {
        Message msg = makeEncryptedMessage(
            key, "alice", "bob",
            "msg at ts=" + std::to_string(ts),
            ts);
        auto dm = decryptMessage(key, msg);
        if (dm) conv.addMessage(std::move(*dm));
    }

    auto sorted = conv.getMessages();
    check("three messages stored", sorted.size() == 3);
    if (sorted.size() == 3) {
        check("first  message ts=100", sorted[0].timestamp == 100);
        check("second message ts=200", sorted[1].timestamp == 200);
        check("third  message ts=300", sorted[2].timestamp == 300);
    }
}

static void testConversationDeduplication() {
    printf("\nTest 7: Conversation::addMessage deduplicates same messageId\n");
    if (!aesAvailable()) { printf("[SKIP] AES-NI unavailable\n"); return; }

    std::vector<uint8_t> key(crypto_aead_aes256gcm_KEYBYTES, 0x77);
    Conversation conv("bob");

    Message msg = makeEncryptedMessage(key, "alice", "bob", "hello", 500);
    auto dm1 = decryptMessage(key, msg);
    auto dm2 = decryptMessage(key, msg);
    if (!dm1 || !dm2) { printf("[SKIP] decryption failed in setup\n"); return; }

    conv.addMessage(*dm1);
    conv.addMessage(*dm2);  // same messageId — should be ignored

    auto msgs = conv.getMessages();
    check("only one message stored after duplicate add", msgs.size() == 1);
}

static void testParseJsonString() {
    printf("\nTest 8: parseJsonString extracts correct value\n");
    std::string json = R"({"sender_id":"alice","recipient_id":"bob","timestamp":99})";

    auto sender = parseJsonString(json, "sender_id");
    check("sender_id extracted",   sender.has_value() && *sender == "alice");

    auto recip = parseJsonString(json, "recipient_id");
    check("recipient_id extracted", recip.has_value() && *recip == "bob");

    auto missing = parseJsonString(json, "nonexistent");
    check("missing key returns nullopt", !missing.has_value());

    // Test escape handling
    std::string jsonEsc = R"({"key":"val\"ue"})";
    auto esc = parseJsonString(jsonEsc, "key");
    check("escaped quote in value handled", esc.has_value() && *esc == "val\"ue");
}

static void testParseJsonInt() {
    printf("\nTest 9: parseJsonInt extracts correct integer\n");
    std::string json = R"({"timestamp":1234567890,"count":0,"neg":-5})";

    auto ts = parseJsonInt(json, "timestamp");
    check("timestamp extracted", ts.has_value() && *ts == 1234567890LL);

    auto zero = parseJsonInt(json, "count");
    check("zero extracted", zero.has_value() && *zero == 0LL);

    auto neg = parseJsonInt(json, "neg");
    check("negative value extracted", neg.has_value() && *neg == -5LL);

    auto missing = parseJsonInt(json, "nonexistent");
    check("missing key returns nullopt", !missing.has_value());
}

static void testB64DecodeRoundTrip() {
    printf("\nTest 10: b64Decode round-trips b64Encode\n");
    unsigned char raw[12];
    randombytes_buf(raw, sizeof(raw));

    std::string encoded = b64Encode(raw, sizeof(raw));
    std::vector<uint8_t> decoded = b64Decode(encoded);

    check("decoded length matches original", decoded.size() == sizeof(raw));
    check("decoded bytes match original",
          decoded.size() == sizeof(raw) &&
          std::memcmp(decoded.data(), raw, sizeof(raw)) == 0);
}

static void testB64DecodeRejectsInvalid() {
    printf("\nTest 11: b64Decode rejects invalid base64\n");
    auto result = b64Decode("not!valid@base64#");
    check("invalid base64 returns empty vector", result.empty());
}

// ============================================================================

int main() {
    if (sodium_init() < 0) {
        printf("FATAL: libsodium init failed\n");
        return 1;
    }
    printf("=== Conversation / message_crypto tests (offline) ===\n");

    testEncryptDecryptRoundTrip();
    testWrongKeyFails();
    testTamperedCiphertextFails();
    testMessageConstructorRejectsShortNonce();
    testMessageConstructorRejectsShortCiphertext();
    testConversationSortOrder();
    testConversationDeduplication();
    testParseJsonString();
    testParseJsonInt();
    testB64DecodeRoundTrip();
    testB64DecodeRejectsInvalid();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}