from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat, PrivateFormat, NoEncryption


class KeyPair:
    def __init__(self):
        self._private_key = X25519PrivateKey.generate()

    def public_key_bytes(self) -> bytes:
        return self._private_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)

    def private_key_bytes(self) -> bytes:
        # Never log or transmit the raw private key — pass it only to the HPKE wrapping step
        return self._private_key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
