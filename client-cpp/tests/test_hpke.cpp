#include "../src/hpke_utils.hpp"
#include "../src/crypto_utils.hpp"
#include "../src/message_crypto.hpp"
#include <sodium.h>
#include <stdio.h>
#include <vector>
#include <cstdint>
#include <algorithm>

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool ok) {
    if (ok) { printf("[PASS] %s\n", label); ++passed; }
    else     { printf("[FAIL] %s\n", label); ++failed; }
}

// ============================================================================
// Test 1: Keypair generation
// ============================================================================
static void testKeypairGeneration() {
    printf("\nTest 1: hpkeGenerateKeypair produces valid 32-byte keys\n");
    HpkeKeypair kp1 = hpkeGenerateKeypair();
    HpkeKeypair kp2 = hpkeGenerateKeypair();

    check("pk is 32 bytes",  kp1.pk.size() == 32);
    check("sk is 32 bytes",  kp1.sk.size() == 32);
    check("two pk are distinct", kp1.pk != kp2.pk);
    check("two sk are distinct", kp1.sk != kp2.sk);
}

// ============================================================================
// Test 2: X25519 DH symmetry — X25519(sk_a, pk_b) == X25519(sk_b, pk_a)
// This is the mathematical basis for why sender and receiver derive the same key.
// ============================================================================
static void testDhSymmetry() {
    printf("\nTest 2: X25519 DH symmetry — X25519(a_sk, b_pk) == X25519(b_sk, a_pk)\n");
    HpkeKeypair alice = hpkeGenerateKeypair();
    HpkeKeypair bob   = hpkeGenerateKeypair();

    unsigned char dh_alice[32], dh_bob[32];
    int rc1 = crypto_scalarmult(dh_alice, alice.sk.data(), bob.pk.data());
    int rc2 = crypto_scalarmult(dh_bob,   bob.sk.data(),   alice.pk.data());

    check("both DH ops succeed", rc1 == 0 && rc2 == 0);
    if (rc1 == 0 && rc2 == 0) {
        check("DH outputs are equal", std::equal(dh_alice, dh_alice + 32, dh_bob));
    }
}

// ============================================================================
// Test 3: HKDF is deterministic and domain-separated
// ============================================================================
static void testHkdfDeterministic() {
    printf("\nTest 3: HKDF is deterministic and domain-separated\n");
    std::vector<uint8_t> salt(16, 0xAA);
    std::vector<uint8_t> ikm(32,  0xBB);

    std::vector<uint8_t> prk1 = hkdfExtract(salt, ikm);
    std::vector<uint8_t> prk2 = hkdfExtract(salt, ikm);
    check("hkdfExtract is deterministic", prk1 == prk2);
    check("hkdfExtract returns 32 bytes", prk1.size() == 32);

    std::vector<uint8_t> okm1 = hkdfExpand32(prk1, "info-A");
    std::vector<uint8_t> okm2 = hkdfExpand32(prk1, "info-A");
    std::vector<uint8_t> okm3 = hkdfExpand32(prk1, "info-B");
    check("hkdfExpand32 is deterministic",             okm1 == okm2);
    check("hkdfExpand32 returns 32 bytes",             okm1.size() == 32);
    check("different info produces different OKM",    okm1 != okm3);

    // Different salt → different PRK → different OKM
    std::vector<uint8_t> salt2(16, 0xCC);
    std::vector<uint8_t> prk3 = hkdfExtract(salt2, ikm);
    std::vector<uint8_t> okm4 = hkdfExpand32(prk3, "info-A");
    check("different salt produces different OKM",    okm1 != okm4);
}

// ============================================================================
// Test 4: hpkeSend + hpkeReceive produce the same AES key (core round-trip)
// ============================================================================
static void testSendReceiveKeyAgreement() {
    printf("\nTest 4: hpkeSend + hpkeReceive agree on the same AES key\n");
    HpkeKeypair alice = hpkeGenerateKeypair();
    HpkeKeypair bob   = hpkeGenerateKeypair();

    auto sendResult = hpkeSend(alice.sk, bob.pk);
    check("hpkeSend succeeds",          sendResult.has_value());
    if (!sendResult) return;

    check("hpkeSend aesKey is 32 bytes", sendResult->aesKey.size() == 32);
    check("hpkeSend ephPk is 32 bytes",  sendResult->ephPk.size()  == 32);

    std::vector<uint8_t> receivedKey = hpkeReceive(bob.sk, sendResult->ephPk, alice.pk);
    check("hpkeReceive returns 32-byte key", receivedKey.size() == 32);
    check("sender and receiver agree on AES key", sendResult->aesKey == receivedKey);
}

// ============================================================================
// Test 5: Wrong sender pk → different key → decryption fails
// Demonstrates sender authentication: Mallory cannot forge Alice's messages.
// ============================================================================
static void testWrongSenderPkFails() {
    printf("\nTest 5: Wrong sender pk causes authentication failure\n");
    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI unavailable\n"); return;
    }
    HpkeKeypair alice   = hpkeGenerateKeypair();
    HpkeKeypair bob     = hpkeGenerateKeypair();
    HpkeKeypair mallory = hpkeGenerateKeypair();

    auto sendResult = hpkeSend(alice.sk, bob.pk);
    if (!sendResult) { printf("[FAIL] hpkeSend failed\n"); ++failed; return; }

    // Bob correctly derives Alice's key
    std::vector<uint8_t> correctKey = hpkeReceive(bob.sk, sendResult->ephPk, alice.pk);
    // Bob mistakenly uses Mallory's pk (server substituted it)
    std::vector<uint8_t> wrongKey   = hpkeReceive(bob.sk, sendResult->ephPk, mallory.pk);

    check("correct sender pk → correct key",   correctKey == sendResult->aesKey);
    check("wrong sender pk → different key",   correctKey != wrongKey);
    check("wrong key is not empty",            !wrongKey.empty());

    // Encrypt a message with Alice's real key; try to decrypt with wrong key
    auto enc = encryptMessage(sendResult->aesKey, "alice", "bob", "secret message");
    check("encrypt with real key succeeds", enc.has_value());
    if (!enc) return;

    // Build a minimal Message object for decryptMessage
    std::vector<uint8_t> nonce = b64Decode(enc->nonceB64);
    std::vector<uint8_t> ct    = b64Decode(enc->ctB64);
    Message msg(enc->messageId, "alice", "bob", ct, nonce, enc->timestamp);

    auto dm1 = decryptMessage(correctKey, msg);
    auto dm2 = decryptMessage(wrongKey,   msg);
    check("correct key decrypts successfully",   dm1.has_value());
    check("wrong key decryption fails (nullopt)", !dm2.has_value());
}

// ============================================================================
// Test 6: Wrong recipient pk → sender derives different key → decryption fails
// ============================================================================
static void testWrongRecipientPkFails() {
    printf("\nTest 6: Wrong recipient pk causes encryption/decryption mismatch\n");
    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI unavailable\n"); return;
    }
    HpkeKeypair alice = hpkeGenerateKeypair();
    HpkeKeypair bob   = hpkeGenerateKeypair();
    HpkeKeypair carol = hpkeGenerateKeypair();

    // Alice encrypts to Carol's key by mistake
    auto sendResult = hpkeSend(alice.sk, carol.pk);
    if (!sendResult) { printf("[FAIL] hpkeSend failed\n"); ++failed; return; }

    // Bob tries to receive using his own key — must produce a 32-byte key (derivation itself
    // succeeds), but that key must differ from what Alice derived (wrong recipient DH).
    std::vector<uint8_t> bobKey = hpkeReceive(bob.sk, sendResult->ephPk, alice.pk);
    check("Bob derives a 32-byte key (hpkeReceive did not fail)", bobKey.size() == 32);
    if (bobKey.size() == 32) {
        check("Bob's key differs from Alice's (wrong recipient)", bobKey != sendResult->aesKey);
    }

    auto enc = encryptMessage(sendResult->aesKey, "alice", "carol", "for carol only");
    check("encrypt succeeds", enc.has_value());
    if (!enc) return;

    std::vector<uint8_t> nonce = b64Decode(enc->nonceB64);
    std::vector<uint8_t> ct    = b64Decode(enc->ctB64);
    Message msg(enc->messageId, "alice", "carol", ct, nonce, enc->timestamp);

    auto bobDecrypt = decryptMessage(bobKey, msg);
    check("Bob cannot decrypt message intended for Carol", !bobDecrypt.has_value());
}

// ============================================================================
// Test 7: Full round-trip — HPKE derive → encrypt → decrypt → plaintext matches
// ============================================================================
static void testFullRoundTrip() {
    printf("\nTest 7: Full HPKE + AES-256-GCM round-trip\n");
    if (crypto_aead_aes256gcm_is_available() == 0) {
        printf("[SKIP] AES-NI unavailable\n"); return;
    }
    HpkeKeypair alice = hpkeGenerateKeypair();
    HpkeKeypair bob   = hpkeGenerateKeypair();

    const std::string plaintext = "End-to-end encrypted with HPKE X25519!";

    // Alice sends
    auto sendResult = hpkeSend(alice.sk, bob.pk);
    check("hpkeSend succeeds", sendResult.has_value());
    if (!sendResult) return;

    auto enc = encryptMessage(sendResult->aesKey, "alice", "bob", plaintext);
    check("encryptMessage succeeds", enc.has_value());
    if (!enc) return;

    // Bob receives
    std::vector<uint8_t> receivedKey = hpkeReceive(bob.sk, sendResult->ephPk, alice.pk);
    check("hpkeReceive returns key", !receivedKey.empty());

    std::vector<uint8_t> nonce = b64Decode(enc->nonceB64);
    std::vector<uint8_t> ct    = b64Decode(enc->ctB64);
    Message msg(enc->messageId, "alice", "bob", ct, nonce, enc->timestamp);

    auto dm = decryptMessage(receivedKey, msg);
    check("decryptMessage succeeds",         dm.has_value());
    check("decrypted plaintext matches",     dm.has_value() && dm->plaintext == plaintext);
    check("senderId preserved",              dm.has_value() && dm->senderId == "alice");
    check("recipientId preserved",           dm.has_value() && dm->recipientId == "bob");
}

// ============================================================================
// Test 8: Low-order point (all-zero ephPk) is rejected by hpkeReceive
// A malicious server could send a crafted kem_output to force a weak shared secret.
// ============================================================================
static void testLowOrderPointRejected() {
    printf("\nTest 8: All-zero ephPk (low-order point) rejected by hpkeReceive\n");
    HpkeKeypair bob   = hpkeGenerateKeypair();
    HpkeKeypair alice = hpkeGenerateKeypair();

    std::vector<uint8_t> zeroEphPk(32, 0x00);
    std::vector<uint8_t> result = hpkeReceive(bob.sk, zeroEphPk, alice.pk);
    check("all-zero ephPk rejected (returns empty)", result.empty());
}

// ============================================================================
// Test 9: Fresh ephemeral keys per send — two sends to same recipient differ
// Demonstrates per-message forward secrecy.
// ============================================================================
static void testEphemeralKeyFreshness() {
    printf("\nTest 9: Each hpkeSend produces a distinct ephemeral pk\n");
    HpkeKeypair alice = hpkeGenerateKeypair();
    HpkeKeypair bob   = hpkeGenerateKeypair();

    auto r1 = hpkeSend(alice.sk, bob.pk);
    auto r2 = hpkeSend(alice.sk, bob.pk);
    check("both sends succeed",            r1.has_value() && r2.has_value());
    if (!r1 || !r2) return;
    check("ephemeral pks differ",          r1->ephPk  != r2->ephPk);
    check("derived AES keys differ",       r1->aesKey != r2->aesKey);
}

// ============================================================================

int main() {
    if (sodium_init() < 0) {
        printf("FATAL: libsodium init failed\n");
        return 1;
    }
    printf("=== HPKE X25519 key exchange tests (offline) ===\n");

    testKeypairGeneration();
    testDhSymmetry();
    testHkdfDeterministic();
    testSendReceiveKeyAgreement();
    testWrongSenderPkFails();
    testWrongRecipientPkFails();
    testFullRoundTrip();
    testLowOrderPointRejected();
    testEphemeralKeyFreshness();

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
