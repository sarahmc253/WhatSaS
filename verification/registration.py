import getpass
import logging
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "crypto-library"))

from keypair import KeyPair
from key_storage import encrypt_private_key, save_to_file
from key_publisher import KeyPublisher

logger = logging.getLogger(__name__)


def register_user(username: str, password: str, server_url: str, auth_token: str) -> bool:
    if not re.fullmatch(r"[A-Za-z0-9_.‐-]{1,64}", username):
        raise ValueError("Invalid username: only letters, numbers, underscores, dots and hyphens are allowed (max 64 characters)")
    if not auth_token:
        raise ValueError("auth_token must not be empty")

    key_path = Path.home() / ".config" / "securemsg" / f"{username}_private_key.json"

    try:
        keypair   = KeyPair.generate()
        encrypted = encrypt_private_key(keypair.private_key_bytes(), password)

        key_path.parent.mkdir(parents=True, exist_ok=True)
        save_to_file(encrypted, key_path)

        KeyPublisher(server_url).publish_public_key(username, auth_token, keypair.public_key_bytes())

    except (ValueError, OSError) as e:
        logger.exception("Registration failed for user %r: %s", username, e)
        if key_path.exists():
            key_path.unlink()
        return False

    return True


if __name__ == "__main__":
    success = register_user(
        username="testuser",
        password=getpass.getpass("Password: "),
        server_url="http://localhost:5000",
        auth_token="test-token",
    )
    print("Registration succeeded" if success else "Registration failed")
