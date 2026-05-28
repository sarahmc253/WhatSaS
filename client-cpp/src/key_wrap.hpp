#ifndef KEY_WRAP_HPP
#define KEY_WRAP_HPP

// key_wrap.hpp — X25519 keypair generation and private-key wrapping.
//
// Mirrors the web client's encryptPrivateKey / decryptPrivateKey in
// client-web/crypto/keyStorage.js so that keys created by the C++ client
// can be unwrapped by the web client and vice-versa.
//
// Wire format for wrapped_private_key (sent to server):
//   base64( JSON({ salt, kek_nonce, wrapped_dek, dek_nonce, ciphertext }) )
//
// Key-derivation chain:
//   Argon2id(password, salt) -> KEK (32 B)
//   HKDF(KEK, "securemsg-dek-wrap-v1", salt) -> wrapKey (32 B)
//   AES-256-GCM(wrapKey, kekNonce) wraps random DEK (32 B) -> wrapped_dek
//   AES-256-GCM(DEK, dekNonce) encrypts X25519 private key bytes -> ciphertext

#include <sodium.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "crypto_utils.hpp"
#include "hkdf_info.hpp"

// ── Argon2id parameters — must match client-web/crypto/kek.js exactly ────────
static constexpr uint64_t ARGON2_MEM_BYTES  = 32768ULL * 1024ULL; // 32 MB
static constexpr uint64_t ARGON2_OPS_LIMIT  = 2;
static constexpr int      ARGON2_PARALLELISM = 4;
static constexpr std::size_t KEK_LEN         = 32;
static constexpr std::size_t SALT_LEN        = 16;
static constexpr std::size_t NONCE_LEN       = 12;
static constexpr std::size_t DEK_LEN         = 32;

struct X25519Keypair {
    std::vector<uint8_t> publicKey;   // 32 bytes
    std::vector<uint8_t> privateKey;  // 32 bytes
};

// Generate a fresh X25519 keypair using libsodium.
inline X25519Keypair generateX25519Keypair() {
    X25519Keypair kp;
    kp.publicKey.resize(crypto_kx_PUBLICKEYBYTES);
    kp.privateKey.resize(crypto_kx_SECRETKEYBYTES);
    crypto_kx_keypair(kp.publicKey.data(), kp.privateKey.data());
    return kp;
}

// Derive 32-byte KEK from password + salt using Argon2id.
inline std::vector<uint8_t> deriveKek(const std::string& password,
                                       const std::vector<uint8_t>& salt) {
    std::vector<uint8_t> kek(KEK_LEN);
    int rc = crypto_pwhash(
        kek.data(), kek.size(),
        password.c_str(), password.size(),
        salt.data(),
        ARGON2_OPS_LIMIT,
        ARGON2_MEM_BYTES,
        crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0)
        throw std::runtime_error("Argon2id key derivation failed (out of memory?)");
    return kek;
}

// HKDF extract+expand using libsodium's HMAC-SHA256 (RFC 5869).
// Matches client-web/crypto/hkdf.js hkdfDerive(keyMaterial, info, salt, length).
// Reuses hkdfExtract/hkdfExpand32 from crypto_utils.hpp — no extra dependency.
inline std::vector<uint8_t> hkdfDeriveOssl(const std::vector<uint8_t>& ikm,
                                             const std::string& info,
                                             const std::vector<uint8_t>& salt,
                                             std::size_t length) {
    // length must be <= 32 (one HMAC-SHA256 block); sufficient for all key material here
    if (length > 32)
        throw std::runtime_error("hkdfDeriveOssl: length > 32 not supported");
    const auto prk = hkdfExtract(salt, ikm);
    if (prk.empty()) throw std::runtime_error("HKDF extract failed");
    const auto okm = hkdfExpand32(prk, info);
    if (okm.empty()) throw std::runtime_error("HKDF expand failed");
    return okm;
}

// AES-256-GCM encrypt with a caller-supplied nonce (not prepended).
// Returns ciphertext+tag (16-byte tag appended by OpenSSL).
inline std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key,
                                            const std::vector<uint8_t>& nonce,
                                            const std::vector<uint8_t>& plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce.size(), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM init failed");
    }

    std::vector<uint8_t> out(plaintext.size() + 16);
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx, out.data(), &outLen,
                          plaintext.data(), (int)plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM encrypt failed");
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, out.data() + outLen, &finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM final failed");
    }
    outLen += finalLen;

    // Append 16-byte auth tag
    unsigned char tag[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM get tag failed");
    }
    EVP_CIPHER_CTX_free(ctx);

    out.resize(outLen);
    out.insert(out.end(), tag, tag + 16);
    return out;
}

struct WrappedKeyResult {
    std::string x25519PublicKeyB64;  // base64 raw public key bytes
    std::string wrappedPrivateKeyB64; // base64(JSON({salt, kek_nonce, wrapped_dek, dek_nonce, ciphertext}))
    std::string kekSaltB64;           // base64 of the 16-byte Argon2 salt
};

// Wraps an X25519 private key with the user's password.
// Matches client-web/crypto/keyStorage.js encryptPrivateKey exactly.
inline WrappedKeyResult wrapPrivateKey(const X25519Keypair& kp,
                                        const std::string& password) {
    // 1. Random 16-byte salt for Argon2
    std::vector<uint8_t> salt(SALT_LEN);
    randombytes_buf(salt.data(), salt.size());

    // 2. Argon2id -> KEK
    const auto kek = deriveKek(password, salt);

    // 3. HKDF(KEK, "securemsg-dek-wrap-v1", salt) -> wrapKey
    const auto wrapKeyBytes = hkdfDeriveOssl(kek, HKDF_INFO_DEK_WRAP, salt, DEK_LEN);

    // 4. Random DEK
    std::vector<uint8_t> dek(DEK_LEN);
    randombytes_buf(dek.data(), dek.size());

    // 5. AES-GCM(wrapKey, kekNonce) wraps DEK
    std::vector<uint8_t> kekNonce(NONCE_LEN);
    randombytes_buf(kekNonce.data(), kekNonce.size());
    const auto wrappedDek = aesGcmEncrypt(wrapKeyBytes, kekNonce, dek);

    // 6. AES-GCM(DEK, dekNonce) encrypts private key
    std::vector<uint8_t> dekNonce(NONCE_LEN);
    randombytes_buf(dekNonce.data(), dekNonce.size());
    const auto ciphertext = aesGcmEncrypt(dek, dekNonce, kp.privateKey);

    // 7. Build JSON matching EncryptedPrivateKey.toJSON() in keyStorage.js
    nlohmann::json inner;
    inner["salt"]        = b64Encode(salt.data(), salt.size());
    inner["kek_nonce"]   = b64Encode(kekNonce.data(), kekNonce.size());
    inner["wrapped_dek"] = b64Encode(wrappedDek.data(), wrappedDek.size());
    inner["dek_nonce"]   = b64Encode(dekNonce.data(), dekNonce.size());
    inner["ciphertext"]  = b64Encode(ciphertext.data(), ciphertext.size());

    const std::string innerJson = inner.dump();

    // 8. base64(JSON) — matches atob(data.wrapped_private_key) in api.js login
    const std::string wrappedB64 = b64Encode(
        reinterpret_cast<const unsigned char*>(innerJson.data()), innerJson.size());

    WrappedKeyResult result;
    result.x25519PublicKeyB64  = b64Encode(kp.publicKey.data(), kp.publicKey.size());
    result.wrappedPrivateKeyB64 = wrappedB64;
    result.kekSaltB64           = b64Encode(salt.data(), salt.size());
    return result;
}

#endif // KEY_WRAP_HPP
