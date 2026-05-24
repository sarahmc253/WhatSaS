import threading
import uuid
from datetime import datetime, timezone

import mysql.connector
from argon2 import PasswordHasher
from argon2.exceptions import VerifyMismatchError
from flask import Blueprint, request, jsonify, current_app
from flask_jwt_extended import create_access_token, jwt_required, get_jwt_identity

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
    'username', 'email', 'password',
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
                (id, username, email, password_hash, password_salt,
                 x25519_public_key, wrapped_private_key, kek_salt,
                 tofu_key_pinned_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                user_id, data['username'], data['email'],
                password_hash, password_salt,
                data['x25519_public_key'], data['wrapped_private_key'],
                data['kek_salt'], now,
            ),
        )
        db.commit()
    except mysql.connector.IntegrityError as e:
        db.rollback()
        if e.errno == 1062:
            return jsonify({'error': 'Username or email already exists'}), 409
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
        'wrapped_private_key': user['wrapped_private_key'],
        'kek_salt': user['kek_salt'],
        'x25519_public_key': user['x25519_public_key'],
    }), 200


@auth_bp.route('/logout', methods=['POST'])
@jwt_required()
def logout():
    if current_app.config.get('ANCHORING_ENABLED'):
        from ..messages.anchor import anchor_pending
        user_id = get_jwt_identity()
        app = current_app._get_current_object()
        threading.Thread(
            target=lambda: _anchor_in_context(app, user_id),
            daemon=True,
        ).start()
    return jsonify({'message': 'Logged out'}), 200


def _anchor_in_context(app, user_id):
    with app.app_context():
        anchor_pending(user_id)
