#ifndef HKDF_INFO_HPP
#define HKDF_INFO_HPP

// Domain-separation info strings for HKDF-Expand (RFC 5869 §2.3).
// Each constant must be unique across roles so a key derived for one
// purpose cannot be reused for another even if the PRK is the same.

// Per-message AES-256-GCM key derived inside DHKEM (hpke_utils.hpp).
static constexpr const char* HKDF_INFO_MSG_ENC = "securemsg-msg-enc-v1";

// Key-encryption key (KEK) derived from password (Argon2) — matches web client.
static constexpr const char* HKDF_INFO_LOCAL_KEK = "securemsg-local-kek-v1";

// Data-encryption key wrap — matches web client HKDF_INFO_DEK_WRAP.
static constexpr const char* HKDF_INFO_DEK_WRAP = "securemsg-dek-wrap-v1";

#endif // HKDF_INFO_HPP
