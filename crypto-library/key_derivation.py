import os
from argon2 import PasswordHasher
from argon2.low_level import Type, hash_secret_raw
from cryptography.hazmat.primitives.hashes import SHA256
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

# OWASP Argon2id recommended minimum: 64 MB memory, 3 iterations, parallelism 4
_hasher = PasswordHasher(
    time_cost=3,
    memory_cost=65536,  # KiB — 64 MB
    parallelism=4,
    hash_len=32,
    salt_len=16,
    type=Type.ID,
)

def hkdf_derive(ikm: bytes, info: bytes, salt: bytes) -> bytes:
    return HKDF(algorithm=SHA256(), length=32, salt=salt, info=info).derive(ikm)


def generate_salt() -> bytes:
    return os.urandom(16)

def derive_local_kek(password: str, salt: bytes) -> bytes:
    # KEK only — used solely to wrap/unwrap the private key, never to encrypt data directly
    return hash_secret_raw(
        secret=password.encode(),
        salt=salt,
        time_cost=2,
        memory_cost=32768,  # KiB — 32 MB
        parallelism=4,
        hash_len=32,
        type=Type.ID,
    )

def hash_password_for_server(password: str) -> str:
    """Return a PHC-format Argon2id hash safe to store directly in the database.

    The returned string encodes the algorithm, version, parameters, salt, and
    digest — no separate salt column is needed.
    """
    return _hasher.hash(password)
