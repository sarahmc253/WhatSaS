import base64
import json
import os
from dataclasses import dataclass
from pathlib import Path
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from key_derivation import derive_local_kek, generate_salt

def encrypt_private_key(private_key_bytes: bytes, password: str) -> "EncryptedPrivateKey":
    salt = generate_salt()
    kek = derive_local_kek(password, salt)

    dek = os.urandom(32)
    kek_nonce = os.urandom(12)
    dek_nonce = os.urandom(12)

    wrapped_dek = AESGCM(kek).encrypt(kek_nonce, dek, None)
    ciphertext  = AESGCM(dek).encrypt(dek_nonce, private_key_bytes, None)

    return EncryptedPrivateKey(
        salt        = salt,
        kek_nonce   = kek_nonce,
        wrapped_dek = wrapped_dek,
        dek_nonce   = dek_nonce,
        ciphertext  = ciphertext,
    )


def decrypt_private_key(encrypted: "EncryptedPrivateKey", password: str) -> bytes:
    try:
        kek             = derive_local_kek(password, encrypted.salt)
        dek             = AESGCM(kek).decrypt(encrypted.kek_nonce, encrypted.wrapped_dek, None)
        private_key_bytes = AESGCM(dek).decrypt(encrypted.dek_nonce, encrypted.ciphertext, None)
    except InvalidTag:
        raise ValueError("Decryption failed: wrong password or corrupted key material")
    return private_key_bytes


@dataclass
class EncryptedPrivateKey:
    salt:        bytes
    kek_nonce:   bytes
    wrapped_dek: bytes
    dek_nonce:   bytes
    ciphertext:  bytes

    def to_dict(self) -> dict:
        return {
            "salt":        base64.b64encode(self.salt).decode(),
            "kek_nonce":   base64.b64encode(self.kek_nonce).decode(),
            "wrapped_dek": base64.b64encode(self.wrapped_dek).decode(),
            "dek_nonce":   base64.b64encode(self.dek_nonce).decode(),
            "ciphertext":  base64.b64encode(self.ciphertext).decode(),
        }

    @classmethod
    def from_dict(cls, data: dict) -> "EncryptedPrivateKey":
        return cls(
            salt        = base64.b64decode(data["salt"]),
            kek_nonce   = base64.b64decode(data["kek_nonce"]),
            wrapped_dek = base64.b64decode(data["wrapped_dek"]),
            dek_nonce   = base64.b64decode(data["dek_nonce"]),
            ciphertext  = base64.b64decode(data["ciphertext"]),
        )


def save_to_file(encrypted: EncryptedPrivateKey, path: "str | Path") -> None:
    p = Path(path)
    p.write_text(json.dumps(encrypted.to_dict(), indent=2), encoding="utf-8")
    os.chmod(p, 0o600)


def load_from_file(path: "str | Path") -> EncryptedPrivateKey:
    return EncryptedPrivateKey.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))
