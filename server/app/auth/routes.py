import base64
import re
import threading
import uuid
from datetime import datetime, timezone

import mysql.connector
from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatchError
from flask import Blueprint, request, jsonify, current_app
from flask_jwt_extended import create_access_token, jwt_required, get_jwt_identity, get_jwt

from .. import get_db

# Argon2id params (explicit to ensure stability across library versions):
# time_cost=3       — number of iterations; increases CPU cost for attackers
# memory_cost=65536 — 64MB RAM required per hash; resists GPU/ASIC brute force
# parallelism=4     — threads per hash; matched to typical server core count
# hash_len=32       — 256-bit output; exceeds minimum security margin
# salt_len=16       — 128-bit random salt; prevents precomputation attacks
ph = PasswordHasher(time_cost=3, memory_cost=65536, parallelism=4, hash_len=32, salt_len=16)
# Pre-computed at startup for timing attack mitigation in /login — see route for usage.
_DUMMY_HASH = ph.hash("dummy")

auth_bp = Blueprint('auth', __name__)

REQUIRED_FIELDS = [
    'username', 'password',
    'x25519_public_key', 'wrapped_private_key', 'kek_salt',
]

def _invalid_fields(data, fields):
    """Return fields that are missing, not a string, or blank after stripping."""
    return [
        f for f in fields
        if not isinstance(data.get(f), str) or not data[f].strip()
    ]

@auth_bp.route('/register', methods=['POST'])
def register():
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, REQUIRED_FIELDS)
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    pw = data['password']
    pw_errors = []
    if len(pw) < 8:
        pw_errors.append('at least 8 characters')
    if not re.search(r'[A-Z]', pw):
        pw_errors.append('one uppercase letter')
    if not re.search(r'[a-z]', pw):
        pw_errors.append('one lowercase letter')
    if not re.search(r'[0-9]', pw):
        pw_errors.append('one number')
    if not re.search(r'[^A-Za-z0-9]', pw):
        pw_errors.append('one special character')
    if pw_errors:
        return jsonify({'error': f"Password must contain: {', '.join(pw_errors)}"}), 400

    password_hash = ph.hash(data['password'])
    # Hash format: $argon2id$v=19$m=...,t=...,p=...$<base64-salt>$<base64-hash>
    password_salt = password_hash.split('$')[4]

    user_id = str(uuid.uuid4())
    now = datetime.now(timezone.utc)

    db = get_db()
    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO users
                (id, username, password_hash, password_salt,
                 x25519_public_key, wrapped_private_key, kek_salt,
                 tofu_key_pinned_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                user_id, data['username'],
                password_hash, password_salt,
                data['x25519_public_key'], data['wrapped_private_key'],
                data['kek_salt'], now,
            ),
        )
        db.commit()
    except mysql.connector.IntegrityError as e:
        db.rollback()
        if e.errno == 1062:
            return jsonify({'error': 'Username already exists'}), 409
        raise
    except Exception:
        db.rollback()
        raise
    finally:
        cursor.close()

    return jsonify({'message': 'User registered successfully'}), 201


@auth_bp.route('/login', methods=['POST'])
def login():
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, ['username', 'password'])
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            """
            SELECT id, username, password_hash,
                   wrapped_private_key, kek_salt, x25519_public_key
            FROM users
            WHERE username = %s
            """,
            (data['username'],),
        )
        user = cursor.fetchone()
    finally:
        cursor.close()

    if user is None:
        # Run a verify against a dummy hash so response time is indistinguishable
        # from a wrong-password attempt — prevents username enumeration via timing.
        try:
            ph.verify(_DUMMY_HASH, data['password'])
        except Exception:
            pass
        return jsonify({'error': 'Invalid credentials'}), 401

    try:
        ph.verify(user['password_hash'], data['password'])
    except VerifyMismatchError:
        return jsonify({'error': 'Invalid credentials'}), 401

    if ph.check_needs_rehash(user['password_hash']):
        new_hash = ph.hash(data['password'])
        new_salt = new_hash.split('$')[4]
        cursor = db.cursor()
        try:
            cursor.execute(
                """
                UPDATE users
                SET password_hash = %s, password_salt = %s
                WHERE id = %s
                """,
                (new_hash, new_salt, user['id']),
            )
            db.commit()
        except Exception as e:
            db.rollback()
            current_app.logger.error("Rehash failed for user %s: %s", user['id'], e)
        finally:
            cursor.close()

    token = create_access_token(
        identity=str(user['id']),
        additional_claims={'username': user['username']},
    )

    return jsonify({
        'token': token,
        'user_id': str(user['id']),
        'wrapped_private_key': user['wrapped_private_key'],
        'kek_salt': user['kek_salt'],
        'x25519_public_key': user['x25519_public_key'],
    }), 200


@auth_bp.route('/change-password', methods=['POST'])
@jwt_required()
def change_password():
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, ['old_password', 'new_password', 'wrapped_private_key', 'kek_salt'])
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    user_id = get_jwt_identity()
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute('SELECT password_hash FROM users WHERE id = %s', (user_id,))
        user = cursor.fetchone()
    finally:
        cursor.close()

    if user is None:
        return jsonify({'error': 'User not found'}), 404

    try:
        ph.verify(user['password_hash'], data['old_password'])
    except VerifyMismatchError:
        return jsonify({'error': 'Old password is incorrect'}), 401

    # Validate re-wrapped key structure before committing.
    # Wire format: base64(JSON({salt, kek_nonce, wrapped_dek, dek_nonce, ciphertext}))
    # kek_salt must be valid base64 decoding to exactly 16 bytes (Argon2id salt).
    KEK_SALT_LEN = 16
    WRAPPED_KEY_FIELDS = {'salt', 'kek_nonce', 'wrapped_dek', 'dek_nonce', 'ciphertext'}
    try:
        wrapped_json_bytes = base64.b64decode(data['wrapped_private_key'])
    except (ValueError, base64.binascii.Error):
        return jsonify({'error': 'wrapped_private_key is not valid base64'}), 400
    try:
        import json as _json
        wrapped_obj = _json.loads(wrapped_json_bytes)
    except (ValueError, UnicodeDecodeError):
        return jsonify({'error': 'wrapped_private_key does not contain valid JSON'}), 400
    missing_fields = WRAPPED_KEY_FIELDS - set(wrapped_obj.keys() if isinstance(wrapped_obj, dict) else [])
    if missing_fields:
        return jsonify({'error': f'wrapped_private_key JSON missing fields: {", ".join(sorted(missing_fields))}'}), 400
    try:
        kek_salt_bytes = base64.b64decode(data['kek_salt'])
    except (ValueError, base64.binascii.Error):
        return jsonify({'error': 'kek_salt is not valid base64'}), 400
    if len(kek_salt_bytes) != KEK_SALT_LEN:
        return jsonify({'error': f'kek_salt must decode to {KEK_SALT_LEN} bytes'}), 400

    new_hash = ph.hash(data['new_password'])
    new_salt = new_hash.split('$')[4]

    cursor = db.cursor()
    try:
        cursor.execute(
            """
            UPDATE users
            SET password_hash = %s, password_salt = %s,
                wrapped_private_key = %s, kek_salt = %s
            WHERE id = %s
            """,
            (new_hash, new_salt,
             data['wrapped_private_key'], data['kek_salt'],
             user_id),
        )
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        cursor.close()

    return jsonify({'message': 'Password changed successfully'}), 200


@auth_bp.route('/logout', methods=['POST'])
@jwt_required()
def logout():
    jti = get_jwt()['jti']
    current_app.revoked_jtis.add(jti)

    if current_app.config.get('ANCHORING_ENABLED'):
        user_id = get_jwt_identity()
        app = current_app._get_current_object()
        threading.Thread(
            target=lambda: _anchor_in_context(app, user_id),
            daemon=True,
        ).start()
    return jsonify({'message': 'Logged out'}), 200


def _anchor_in_context(app, user_id):
    from ..messages.anchor import anchor_pending
    with app.app_context():
        anchor_pending(user_id)
