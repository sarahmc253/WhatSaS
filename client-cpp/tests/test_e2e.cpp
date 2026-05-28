#define NOMINMAX
#include "../src/crypto_utils.hpp"
#include "../src/hpke_utils.hpp"
#include "../src/message_crypto.hpp"
#include "../include/Conversation.hpp"
#include "../include/Message.hpp"
#include "../include/MessageStore.hpp"
#include <sodium.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int passed = 0;
static int failed = 0;

static void check(const char* label, bool ok) {
    if (ok) { printf("[PASS] %s\n", label); ++passed; }
    else     { printf("[FAIL] %s\n", label); ++failed; }
}

// ── Mock server storage ───────────────────────────────────────────────────────

struct MockUserRecord {
    std::string x25519PublicKey;    // base64(32 bytes)
    std::string wrappedPrivateKey;  // base64(nonce || ciphertext+tag)
    std::string kekSalt;            // base64(crypto_pwhash_SALTBYTES)
};

struct MockMessage {
    std::string senderId;
    std::string recipientId;
    std::string messageId;
    std::string nonceB64;
    std::string ctB64;
    std::string kemOutput;  // base64(ephemeral public key)
    std::time_t timestamp;
};

// ── Client-side helpers (mirror main.cpp logic) ───────────────────────────────

// Simulate what the client does during registration: generate keypair,
// derive KEK, wrap private key. Returns false on any crypto failure.
static bool clientRegister(const std::string& password, MockUserRecord& out) {
    std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
    std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
    crypto_box_keypair(pk.data(), sk.data());

    std::vector<uint8_t> kekSalt(crypto_pwhash_SALTBYTES);
    randombytes_buf(kekSalt.data(), kekSalt.size());

    std::vector<uint8_t> kek(32);
    if (crypto_pwhash(kek.data(), kek.size(),
                      password.c_str(), password.size(),
                      kekSalt.data(),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return false;
    }

    unsigned char wrapNonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(wrapNonce, sizeof(wrapNonce));

    std::vector<uint8_t> wrapped(
        sizeof(wrapNonce) + crypto_box_SECRETKEYBYTES + crypto_aead_aes256gcm_ABYTES);
    unsigned long long wrappedLen = 0;
    if (crypto_aead_aes256gcm_encrypt(
            wrapped.data() + sizeof(wrapNonce), &wrappedLen,
            sk.data(), sk.size(), nullptr, 0, nullptr,
            wrapNonce, kek.data()) != 0) {
        return false;
    }
    std::copy(wrapNonce, wrapNonce + sizeof(wrapNonce), wrapped.begin());
    wrapped.resize(sizeof(wrapNonce) + wrappedLen);

    out.x25519PublicKey   = b64Encode(pk.data(), pk.size());
    out.wrappedPrivateKey = b64Encode(wrapped.data(), wrapped.size());
    out.kekSalt           = b64Encode(kekSalt.data(), kekSalt.size());
    return true;
}

// Simulate what the client does during login: retrieve key material from the
// server record, re-derive KEK, unwrap private key, re-derive public key.
static bool clientLogin(const std::string& password, const MockUserRecord& record,
                        std::vector<uint8_t>& outSk, std::vector<uint8_t>& outPk) {
    const auto kekSaltBytes = b64Decode(record.kekSalt);
    if (kekSaltBytes.size() != crypto_pwhash_SALTBYTES) return false;

    std::vector<uint8_t> kek(32);
    if (crypto_pwhash(kek.data(), kek.size(),
                      password.c_str(), password.size(),
                      kekSaltBytes.data(),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return false;
    }

    const auto wrappedBytes = b64Decode(record.wrappedPrivateKey);
    constexpr std::size_t nonceLen = crypto_aead_aes256gcm_NPUBBYTES;
    constexpr std::size_t tagLen   = crypto_aead_aes256gcm_ABYTES;
    if (wrappedBytes.size() != nonceLen + crypto_box_SECRETKEYBYTES + tagLen) return false;

    std::vector<uint8_t> sk(crypto_box_SECRETKEYBYTES);
    unsigned long long skLen = 0;
    if (crypto_aead_aes256gcm_decrypt(
            sk.data(), &skLen, nullptr,
            wrappedBytes.data() + nonceLen, wrappedBytes.size() - nonceLen,
            nullptr, 0,
            wrappedBytes.data(),
            kek.data()) != 0) {
        return false;
    }
    sk.resize(skLen);

    std::vector<uint8_t> pk(crypto_box_PUBLICKEYBYTES);
    crypto_scalarmult_base(pk.data(), sk.data());

    outSk = std::move(sk);
    outPk = std::move(pk);
    return true;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testRegisterLoginSendReceive() {
    printf("\nTest: register -> login -> send -> receive\n");

    // ── Register ──────────────────────────────────────────────────────────────
    MockUserRecord aliceRecord, bobRecord;
    check("alice registers",  clientRegister("aliceP@ss1!", aliceRecord));
    check("bob registers",    clientRegister("bobP@ss2#",  bobRecord));

    // ── Login ─────────────────────────────────────────────────────────────────
    std::vector<uint8_t> aliceSk, alicePk, bobSk, bobPk;
    check("alice logs in",  clientLogin("aliceP@ss1!", aliceRecord, aliceSk, alicePk));
    check("bob logs in",    clientLogin("bobP@ss2#",  bobRecord,  bobSk,  bobPk));

    // Recovered public keys must match what was stored at registration.
    check("alice recovered pk matches registered pk",
          alicePk == b64Decode(aliceRecord.x25519PublicKey));
    check("bob recovered pk matches registered pk",
          bobPk == b64Decode(bobRecord.x25519PublicKey));

    // ── Send (Alice -> Bob) ───────────────────────────────────────────────────
    const std::string plaintext = "hello bob, this is alice!";

    auto hpkeResult = hpkeSend(aliceSk, bobPk);
    check("HPKE send succeeds", hpkeResult.has_value());
    if (!hpkeResult) return;

    auto enc = encryptMessage(hpkeResult->aesKey, "alice", "bob", plaintext);
    sodium_memzero(hpkeResult->aesKey.data(), hpkeResult->aesKey.size());
    check("encrypt message succeeds", enc.has_value());
    if (!enc) return;

    // Store on mock server.
    MockMessage msg;
    msg.senderId    = "alice";
    msg.recipientId = "bob";
    msg.messageId   = enc->messageId;
    msg.nonceB64    = enc->nonceB64;
    msg.ctB64       = enc->ctB64;
    msg.kemOutput   = b64Encode(hpkeResult->ephPk.data(), hpkeResult->ephPk.size());
    msg.timestamp   = enc->timestamp;

    // ── Receive (Bob) ─────────────────────────────────────────────────────────
    const auto ephPk = b64Decode(msg.kemOutput);
    check("kem_output decodes to 32 bytes", ephPk.size() == 32);
    if (ephPk.size() != 32) return;

    auto aesKey = hpkeReceive(bobSk, ephPk, alicePk);
    check("HPKE receive produces key", !aesKey.empty());
    if (aesKey.empty()) return;

    const auto nonce = b64Decode(msg.nonceB64);
    const auto ct    = b64Decode(msg.ctB64);

    Message received(msg.messageId, msg.senderId, msg.recipientId,
                     ct, nonce, msg.timestamp);
    auto decrypted = decryptMessage(aesKey, received);
    sodium_memzero(aesKey.data(), aesKey.size());

    check("decrypt succeeds",         decrypted.has_value());
    check("plaintext matches",        decrypted && decrypted->plaintext == plaintext);
    check("sender ID matches",        decrypted && decrypted->senderId == "alice");
    check("recipient ID matches",     decrypted && decrypted->recipientId == "bob");

    // ── MessageStore + Conversation integration ───────────────────────────────
    if (decrypted) {
        MessageStore store;
        store.addMessage(received, peerKey("alice", "bob"));
        check("store holds the message",   store.size() == 1);
        check("hasMessage by ID",          store.hasMessage(msg.messageId));
        check("getMessagesFor peer bucket",
              store.getMessagesFor(peerKey("alice", "bob")).size() == 1);

        Conversation conv("alice");
        conv.addMessage(*decrypted);
        check("conversation holds one message",    conv.getMessages().size() == 1);
        check("conversation deduplicates on re-add",
              (conv.addMessage(*decrypted), conv.getMessages().size() == 1));
        check("messageCount for alice",    conv.messageCount("alice") == 1);
        check("messageCount for bob is 0", conv.messageCount("bob") == 0);
    }
}

static void testWrongPasswordRejected() {
    printf("\nTest: wrong password fails unwrap\n");

    MockUserRecord record;
    check("register succeeds", clientRegister("correctP@ss!", record));

    std::vector<uint8_t> sk, pk;
    check("wrong password rejected", !clientLogin("wrongpassword", record, sk, pk));
    check("no key material on failure", sk.empty() && pk.empty());
}

static void testCrossUserKeyMismatch() {
    printf("\nTest: Bob's key cannot decrypt Alice's messages\n");

    MockUserRecord aliceRecord, bobRecord;
    clientRegister("aliceP@ss1!", aliceRecord);
    clientRegister("bobP@ss2#",  bobRecord);

    std::vector<uint8_t> aliceSk, alicePk, bobSk, bobPk;
    clientLogin("aliceP@ss1!", aliceRecord, aliceSk, alicePk);
    clientLogin("bobP@ss2#",  bobRecord,  bobSk,  bobPk);

    // Alice sends to Bob.
    auto hpkeResult = hpkeSend(aliceSk, bobPk);
    if (!hpkeResult) { check("hpkeSend for cross-user test", false); return; }
    auto enc = encryptMessage(hpkeResult->aesKey, "alice", "bob", "secret");
    sodium_memzero(hpkeResult->aesKey.data(), hpkeResult->aesKey.size());
    if (!enc) { check("encryptMessage for cross-user test", false); return; }

    const auto ephPk = hpkeResult->ephPk;

    // Alice tries to decrypt using her own key as the recipient — wrong DH path.
    auto wrongKey = hpkeReceive(aliceSk, ephPk, alicePk);
    const auto nonce = b64Decode(enc->nonceB64);
    const auto ct    = b64Decode(enc->ctB64);
    Message msg(enc->messageId, "alice", "bob", ct, nonce, enc->timestamp);
    auto result = wrongKey.empty() ? std::nullopt : decryptMessage(wrongKey, msg);
    if (!wrongKey.empty()) sodium_memzero(wrongKey.data(), wrongKey.size());
    check("alice cannot decrypt her own message sent to bob", !result.has_value());
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium init failed\n");
        return 1;
    }
    if (!crypto_aead_aes256gcm_is_available()) {
        printf("[SKIP] AES-256-GCM unavailable (no hardware AES-NI)\n");
        return 0;
    }

    testRegisterLoginSendReceive();
    testWrongPasswordRejected();
    testCrossUserKeyMismatch();

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
