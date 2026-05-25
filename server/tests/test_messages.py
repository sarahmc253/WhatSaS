import uuid
from unittest.mock import MagicMock, patch

import pytest
from server.app.auth.routes import ph as _ph

PASSWORD = 'TestPass123!'

# Precomputed once at module load — argon2id is intentionally slow.
_HASH_A = _ph.hash(PASSWORD)
_HASH_B = _ph.hash(PASSWORD)

USER_A_ID = str(uuid.uuid4())
USER_B_ID = str(uuid.uuid4())
MESSAGE_ID = str(uuid.uuid4())


def _db(fetchone=None):
    """Return a minimal mock DB whose cursor yields fetchone on demand."""
    cursor = MagicMock()
    cursor.fetchone.return_value = fetchone
    db = MagicMock()
    db.cursor.return_value = cursor
    return db


def _register(client, username, email):
    with patch('server.app.auth.routes.get_db', return_value=_db()):
        r = client.post('/auth/register', json={
            'username': username,
            'email': email,
            'password': PASSWORD,
            'x25519_public_key': f'pk_{username}',
            'wrapped_private_key': f'wpk_{username}',
            'kek_salt': f'kek_{username}',
        })
    assert r.status_code == 201, r.get_json()


def _login(client, username, user_id, pw_hash):
    with patch('server.app.auth.routes.get_db', return_value=_db({
        'id': user_id,
        'username': username,
        'password_hash': pw_hash,
        'wrapped_private_key': f'wpk_{username}',
        'kek_salt': f'kek_{username}',
        'x25519_public_key': f'pk_{username}',
    })):
        r = client.post('/auth/login', json={'username': username, 'password': PASSWORD})
    assert r.status_code == 200, r.get_json()
    return r.get_json()['token']


def test_user_b_cannot_read_user_a_message(client):
    _register(client, 'alice', 'alice@test.com')
    _register(client, 'bob', 'bob@test.com')

    token_a = _login(client, 'alice', USER_A_ID, _HASH_A)
    token_b = _login(client, 'bob', USER_B_ID, _HASH_B)

    # User A sends a message with a valid payload; DB is mocked.
    valid_payload = {
        'recipient_id': USER_B_ID,
        'ciphertext': 'deadbeef',
        'nonce': 'cafebabe',
        'content_hash': 'a' * 64,
    }
    with patch('server.app.messages.routes.get_db', return_value=_db()):
        r = client.post(
            '/messages',
            json=valid_payload,
            headers={'Authorization': f'Bearer {token_a}'},
        )
    assert r.status_code == 201

    # DB shows user A owns the message; recipient is a third party, not user B.
    third_party_id = str(uuid.uuid4())
    with patch('server.app.messages.routes.get_db', return_value=_db({
        'sender_id': USER_A_ID,
        'recipient_id': third_party_id,
    })):
        r = client.get(
            f'/messages/{MESSAGE_ID}',
            headers={'Authorization': f'Bearer {token_b}'},
        )

    assert r.status_code == 403
