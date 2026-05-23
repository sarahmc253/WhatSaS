import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "crypto-library"))

from keypair import KeyPair
from key_storage import encrypt_private_key, save_to_file
from key_publisher import KeyPublisher


def register_user(username: str, password: str, server_url: str) -> bool:
    key_path = Path.home() / ".config" / "securemsg" / f"{username}_private_key.json"

    try:
        keypair   = KeyPair.generate()
        encrypted = encrypt_private_key(keypair.private_key_bytes(), password)

        key_path.parent.mkdir(parents=True, exist_ok=True)
        save_to_file(encrypted, key_path)

        # TODO: POST registration to server — replace this placeholder with the
        #       auth token returned from the registration response.
        auth_token = ""
        KeyPublisher(server_url).publish_public_key(username, auth_token, keypair.public_key_bytes())

    except Exception:
        if key_path.exists():
            key_path.unlink()
        return False

    return True


if __name__ == "__main__":
    success = register_user(
        username="testuser",
        password="TestPassword123!",
        server_url="http://localhost:5000",
    )
    print("Registration succeeded" if success else "Registration failed")
