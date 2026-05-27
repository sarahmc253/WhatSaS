#ifndef HKDF_INFO_HPP
#define HKDF_INFO_HPP

// Domain-separation info strings for HKDF-Expand (RFC 5869 §2.3).
// Each constant must be unique across roles so a key derived for one
// purpose cannot be reused for another even if the PRK is the same.

// Per-message AES-256-GCM key derived inside DHKEM (hpke_utils.hpp).
static constexpr const char* HKDF_INFO_MSG_ENC = "WhatSaS-msg-enc-v1";

// Key-encryption key (KEK) used to wrap/unwrap the user's long-term
// X25519 private key stored at rest (derived from password via Argon2).
static constexpr const char* HKDF_INFO_KEY_AT_REST = "WhatSaS-key-at-rest-v1";

#endif // HKDF_INFO_HPP
