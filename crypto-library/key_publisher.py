import base64

import requests


class KeyPublisher:
    def __init__(self, server_url: str) -> None:
        self.server_url = server_url.rstrip("/")

    def publish_public_key(self, username: str, auth_token: str, public_key_bytes: bytes) -> None:
        # Never set verify=False — disabling TLS verification exposes the public key
        # to interception and defeats the trust model of the key exchange.
        resp = requests.post(
            self.server_url + "/api/keys/publish",
            json={
                "username":   username,
                "public_key": base64.b64encode(public_key_bytes).decode(),
            },
            headers={"Authorization": f"Bearer {auth_token}"},
            verify=True,
            timeout=(3.05, 10),
        )
        resp.raise_for_status()
