#ifndef KEY_WRAP_HPP
#define KEY_WRAP_HPP

// key_wrap.hpp — X25519 keypair generation and private-key wrapping.
//
// Mirrors the web client's encryptPrivateKey / decryptPrivateKey in
// client-web/crypto/keyStorage.js so that accounts are usable from both clients.
//
// Wire format for wrapped_private_key sent to / received from the server:
//   base64( JSON({ salt, kek_nonce, wrapped_dek, dek_nonce, ciphertext }) )
//
// Key-derivation chain:
//   Argon2id(password, salt, mem=32 MB, t=2, p=4) -> KEK (32 B)
//   HKDF-SHA256(KEK, info="securemsg-dek-wrap-v1", salt) -> wrapKey (32 B)
//   AES-256-GCM(wrapKey, kekNonce) wraps random DEK (32 B) -> wrapped_dek
//   AES-256-GCM(DEK, dekNonce) encrypts X25519 private key in PKCS8 -> ciphertext
//
// The inner plaintext is the X25519 key in PKCS8 PrivateKeyInfo format (48 bytes).
// That is the only format Web Crypto API can importKey('pkcs8', ...), so both
// C++ registrations and web registrations store the same inner encoding.

#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
#  include <openssl/kdf.h>
#  include <openssl/params.h>
#endif
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "crypto_utils.hpp"
#include "hkdf_info.hpp"

// Argon2id parameters — must match client-web/crypto/kek.js and
// crypto-library/key_derivation.py derive_local_kek exactly.
static constexpr uint32_t ARGON2_MEM_KIB    = 32768;              // 32 MB, for OpenSSL EVP_KDF
static constexpr uint64_t ARGON2_MEM_BYTES  = 32768ULL * 1024ULL; // 32 MB, for libsodium fallback
static constexpr uint32_t ARGON2_OPS_LIMIT  = 2;
static constexpr uint32_t ARGON2_PARALLELISM = 4;
static constexpr std::size_t KEK_LEN         = 32;
static constexpr std::size_t SALT_LEN        = 16;
static constexpr std::size_t NONCE_LEN       = 12;
static constexpr std::size_t DEK_LEN         = 32;

// ASN.1 DER header for an X25519 private key in PKCS8 PrivateKeyInfo:
//   SEQUENCE(46) { INTEGER(0), SEQUENCE { OID 1.3.101.110 }, OCTET STRING { OCTET STRING(32) { key } } }
// = 16 bytes of header followed by the 32 raw key bytes = 48 bytes total.
// Web Crypto API always exports/imports X25519 private keys in this format.
static const uint8_t PKCS8_X25519_HEADER[16] = {
    0x30, 0x2e,                          // SEQUENCE, 46 bytes
    0x02, 0x01, 0x00,                    // INTEGER 0 (version)
    0x30, 0x05,                          // SEQUENCE, 5 bytes
    0x06, 0x03, 0x2b, 0x65, 0x6e,        // OID 1.3.101.110 (X25519)
    0x04, 0x22,                          // OCTET STRING, 34 bytes
    0x04, 0x20,                          // OCTET STRING, 32 bytes (raw key follows)
};

struct X25519Keypair {
    std::vector<uint8_t> publicKey;   // 32 bytes, raw
    std::vector<uint8_t> privateKey;  // 32 bytes, raw
};

// Prepend the 16-byte PKCS8 header to a raw 32-byte X25519 private key.
inline std::vector<uint8_t> toPkcs8(const std::vector<uint8_t>& rawKey) {
    if (rawKey.size() != 32)
        throw std::runtime_error("toPkcs8: raw key must be 32 bytes");
    std::vector<uint8_t> pkcs8(48);
    std::memcpy(pkcs8.data(),      PKCS8_X25519_HEADER, 16);
    std::memcpy(pkcs8.data() + 16, rawKey.data(),       32);
    return pkcs8;
}

// Strip the 16-byte PKCS8 header from a 48-byte X25519 PKCS8 key.
// Validates the expected header before extracting.
inline std::vector<uint8_t> fromPkcs8(const std::vector<uint8_t>& pkcs8Key) {
    if (pkcs8Key.size() != 48)
        throw std::runtime_error("fromPkcs8: PKCS8 X25519 key must be 48 bytes");
    if (std::memcmp(pkcs8Key.data(), PKCS8_X25519_HEADER, 16) != 0)
        throw std::runtime_error("fromPkcs8: unexpected PKCS8 header — not an X25519 key");
    return std::vector<uint8_t>(pkcs8Key.begin() + 16, pkcs8Key.end());
}

// Generate a fresh X25519 keypair using libsodium's CSPRNG.
inline X25519Keypair generateX25519Keypair() {
    X25519Keypair kp;
    kp.publicKey.resize(crypto_box_PUBLICKEYBYTES);
    kp.privateKey.resize(crypto_box_SECRETKEYBYTES);
    crypto_box_keypair(kp.publicKey.data(), kp.privateKey.data());
    return kp;
}

// Derive a 32-byte KEK from password + salt using Argon2id(t=2, m=32 MB, p=4).
//
// Uses OpenSSL 3.2+ EVP_KDF when available — the only path that correctly
// implements p=4 and therefore matches the web client (argon2-browser) and
// the Python reference (key_derivation.py).
//
// Falls back to libsodium on older OpenSSL. Libsodium's Argon2id runs with p=1
// internally, so the derived KEK will differ from p=4 for the same inputs.
// Accounts registered on the fallback path will not interoperate with the web.
inline std::vector<uint8_t> deriveKek(const std::string& password,
                                       const std::vector<uint8_t>& salt) {
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
    {
        // OpenSSL 3.2+: Argon2id KDF with correct parallelism
        EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
        if (kdf) {
            EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
            EVP_KDF_free(kdf);
            if (!ctx) throw std::runtime_error("EVP_KDF_CTX_new failed");

            uint32_t iter    = ARGON2_OPS_LIMIT;
            uint32_t memcost = ARGON2_MEM_KIB;
            uint32_t threads = 1;                  // OpenSSL needs a thread pool for threads>1; Argon2 output is determined by lanes, not thread count
            uint32_t lanes   = ARGON2_PARALLELISM;

            OSSL_PARAM params[] = {
                OSSL_PARAM_construct_octet_string("pass",    const_cast<char*>(password.data()), password.size()),
                OSSL_PARAM_construct_octet_string("salt",    const_cast<uint8_t*>(salt.data()),  salt.size()),
                OSSL_PARAM_construct_uint32("iter",          &iter),
                OSSL_PARAM_construct_uint32("memcost",       &memcost),
                OSSL_PARAM_construct_uint32("threads",       &threads),
                OSSL_PARAM_construct_uint32("lanes",         &lanes),
                OSSL_PARAM_END
            };

            std::vector<uint8_t> kek(KEK_LEN);
            int rc = EVP_KDF_derive(ctx, kek.data(), kek.size(), params);
            EVP_KDF_CTX_free(ctx);

            if (rc == 1) return kek;
            // EVP_KDF_derive returned <= 0: fall through to libsodium
        }
    }
#endif

    // libsodium fallback (Argon2id, but p=1 — differs from web/Python which use p=4)
    std::vector<uint8_t> kek(KEK_LEN);
    if (crypto_pwhash(
            kek.data(), kek.size(),
            password.c_str(), password.size(),
            salt.data(),
            static_cast<unsigned long long>(ARGON2_OPS_LIMIT),
            ARGON2_MEM_BYTES,
            crypto_pwhash_ALG_ARGON2ID13) != 0)
        throw std::runtime_error("Argon2id key derivation failed — not enough memory");
    return kek;
}

// HKDF extract+expand matching client-web/crypto/hkdf.js hkdfDerive().
inline std::vector<uint8_t> hkdfDeriveOssl(const std::vector<uint8_t>& ikm,
                                             const std::string& info,
                                             const std::vector<uint8_t>& salt,
                                             std::size_t length) {
    if (length == 0 || length > 32)
        throw std::runtime_error("hkdfDeriveOssl: length must be 1–32");
    const auto prk = hkdfExtract(salt, ikm);
    if (prk.empty()) throw std::runtime_error("HKDF extract failed");
    auto okm = hkdfExpand32(prk, info);
    if (okm.empty()) throw std::runtime_error("HKDF expand failed");
    okm.resize(length);
    return okm;
}

// AES-256-GCM encrypt. nonce is separate (caller-generated, 12 bytes).
// Returns ciphertext || 16-byte authentication tag.
inline std::vector<uint8_t> aesGcmEncrypt(const std::vector<uint8_t>& key,
                                            const std::vector<uint8_t>& nonce,
                                            const std::vector<uint8_t>& plaintext) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce.size(), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM encrypt init failed");
    }

    std::vector<uint8_t> out(plaintext.size());
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx, out.data(), &outLen,
                          plaintext.data(), (int)plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM encrypt update failed");
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, out.data() + outLen, &finalLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM encrypt final failed");
    }
    out.resize(outLen + finalLen);

    unsigned char tag[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM get tag failed");
    }
    EVP_CIPHER_CTX_free(ctx);

    out.insert(out.end(), tag, tag + 16);
    return out;
}

// AES-256-GCM decrypt. ciphertextWithTag = ciphertext || 16-byte tag.
// Throws std::runtime_error if authentication fails.
inline std::vector<uint8_t> aesGcmDecrypt(const std::vector<uint8_t>& key,
                                            const std::vector<uint8_t>& nonce,
                                            const std::vector<uint8_t>& ciphertextWithTag) {
    if (ciphertextWithTag.size() < 16)
        throw std::runtime_error("AES-GCM input too short to contain a tag");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    const std::size_t ctLen = ciphertextWithTag.size() - 16;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce.size(), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM decrypt init failed");
    }

    std::vector<uint8_t> pt(ctLen);
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx, pt.data(), &outLen,
                          ciphertextWithTag.data(), (int)ctLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM decrypt update failed");
    }

    // Tag must be set before EVP_DecryptFinal_ex
    void* tagPtr = const_cast<uint8_t*>(ciphertextWithTag.data() + ctLen);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tagPtr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("AES-GCM set tag failed");
    }

    int finalLen = 0;
    int rc = EVP_DecryptFinal_ex(ctx, pt.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (rc != 1)
        throw std::runtime_error("AES-GCM authentication failed — wrong password or corrupted key material");

    pt.resize(outLen + finalLen);
    return pt;
}

struct WrappedKeyResult {
    std::string x25519PublicKeyB64;     // base64 raw public key (32 bytes)
    std::string wrappedPrivateKeyB64;   // base64( JSON({salt,kek_nonce,wrapped_dek,dek_nonce,ciphertext}) )
    std::string kekSaltB64;             // base64 of the 16-byte Argon2 salt (also stored inside bundle)
};

// Encrypt an X25519 private key under the user's password.
// Matches client-web/crypto/keyStorage.js encryptPrivateKey exactly.
// Inner plaintext is PKCS8(privateKey) so the web client can importKey('pkcs8', ...).
inline WrappedKeyResult wrapPrivateKey(const X25519Keypair& kp,
                                        const std::string& password) {
    // 1. Random 16-byte Argon2 salt
    std::vector<uint8_t> salt(SALT_LEN);
    randombytes_buf(salt.data(), salt.size());

    // 2. Argon2id → KEK
    const auto kek = deriveKek(password, salt);

    // 3. HKDF(KEK, "securemsg-dek-wrap-v1", salt) → wrapKey
    const auto wrapKeyBytes = hkdfDeriveOssl(kek, HKDF_INFO_DEK_WRAP, salt, DEK_LEN);

    // 4. Random DEK
    std::vector<uint8_t> dek(DEK_LEN);
    randombytes_buf(dek.data(), dek.size());

    // 5. AES-GCM(wrapKey, kekNonce) wraps DEK → wrapped_dek (48 bytes: 32 ciphertext + 16 tag)
    std::vector<uint8_t> kekNonce(NONCE_LEN);
    randombytes_buf(kekNonce.data(), kekNonce.size());
    const auto wrappedDek = aesGcmEncrypt(wrapKeyBytes, kekNonce, dek);

    // 6. AES-GCM(DEK, dekNonce) encrypts private key in PKCS8 format
    //    Web Crypto API requires PKCS8 for importKey; C++ strips the header on login
    std::vector<uint8_t> dekNonce(NONCE_LEN);
    randombytes_buf(dekNonce.data(), dekNonce.size());
    const auto ciphertext = aesGcmEncrypt(dek, dekNonce, toPkcs8(kp.privateKey));

    // 7. JSON bundle matching EncryptedPrivateKey.toJSON() in keyStorage.js
    nlohmann::json inner;
    inner["salt"]        = b64Encode(salt.data(),       salt.size());
    inner["kek_nonce"]   = b64Encode(kekNonce.data(),   kekNonce.size());
    inner["wrapped_dek"] = b64Encode(wrappedDek.data(), wrappedDek.size());
    inner["dek_nonce"]   = b64Encode(dekNonce.data(),   dekNonce.size());
    inner["ciphertext"]  = b64Encode(ciphertext.data(), ciphertext.size());

    const std::string innerJson = inner.dump();

    WrappedKeyResult result;
    result.x25519PublicKeyB64   = b64Encode(kp.publicKey.data(), kp.publicKey.size());
    result.wrappedPrivateKeyB64 = b64Encode(
        reinterpret_cast<const uint8_t*>(innerJson.data()), innerJson.size());
    result.kekSaltB64           = b64Encode(salt.data(), salt.size());
    return result;
}

// Decrypt a wrapped_private_key bundle from the server login response.
// Returns the raw 32-byte X25519 private key.
// Handles:
//   - PKCS8 inner encoding (48 bytes): web client registrations and new C++ registrations
//   - Raw inner encoding (32 bytes): old C++ registrations using the previous single-layer scheme
inline std::vector<uint8_t> unwrapPrivateKey(const std::string& wrappedPrivateKeyB64,
                                               const std::string& /*kekSaltB64*/,
                                               const std::string& password) {
    // 1. base64-decode outer envelope → raw JSON text
    const auto jsonBytes = b64Decode(wrappedPrivateKeyB64);
    if (jsonBytes.empty())
        throw std::runtime_error("wrapped_private_key: invalid base64");

    // 2. Parse JSON bundle
    nlohmann::json bundle;
    try {
        bundle = nlohmann::json::parse(jsonBytes.begin(), jsonBytes.end());
    } catch (const nlohmann::json::exception& ex) {
        throw std::runtime_error(
            std::string("wrapped_private_key: JSON parse error: ") + ex.what());
    }

    auto getField = [&](const char* name) -> std::vector<uint8_t> {
        if (!bundle.contains(name) || !bundle[name].is_string())
            throw std::runtime_error(std::string("wrapped_private_key missing field: ") + name);
        const auto v = b64Decode(bundle[name].get<std::string>());
        if (v.empty())
            throw std::runtime_error(std::string("wrapped_private_key: bad base64 in field: ") + name);
        return v;
    };

    const auto salt       = getField("salt");
    const auto kekNonce   = getField("kek_nonce");
    const auto wrappedDek = getField("wrapped_dek");
    const auto dekNonce   = getField("dek_nonce");
    const auto ciphertext = getField("ciphertext");

    // 3. Argon2id → KEK (uses salt from inside the bundle, which is authoritative)
    const auto kek = deriveKek(password, salt);

    // 4. HKDF → wrapKey
    const auto wrapKeyBytes = hkdfDeriveOssl(kek, HKDF_INFO_DEK_WRAP, salt, DEK_LEN);

    // 5. Unwrap DEK
    const auto dek = aesGcmDecrypt(wrapKeyBytes, kekNonce, wrappedDek);
    if (dek.size() != DEK_LEN)
        throw std::runtime_error("unwrapped DEK has unexpected length");

    // 6. Decrypt inner private key bytes
    const auto skBytes = aesGcmDecrypt(dek, dekNonce, ciphertext);

    // 7. Normalise to raw 32-byte key:
    //    48 bytes → PKCS8 (web client or new C++ registrations): strip the 16-byte header
    //    32 bytes → raw (old C++ single-layer registrations): use as-is
    if (skBytes.size() == 48) return fromPkcs8(skBytes);
    if (skBytes.size() == 32) return skBytes;

    throw std::runtime_error(
        "decrypted private key has unexpected length " + std::to_string(skBytes.size()) +
        " — expected 48 (PKCS8) or 32 (raw)");
}

#endif // KEY_WRAP_HPP
