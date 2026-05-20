from flask import Blueprint, request, jsonify
from argon2 import PasswordHasher
import uuid
from datetime import datetime, timezone
import mysql.connector
from .. import get_db

# Argon2id params (explicit to ensure stability across library versions):
# time_cost=3       — number of iterations; increases CPU cost for attackers
# memory_cost=65536 — 64MB RAM required per hash; resists GPU/ASIC brute force
# parallelism=4     — threads per hash; matched to typical server core count
# hash_len=32       — 256-bit output; exceeds minimum security margin
# salt_len=16       — 128-bit random salt; prevents precomputation attacks
ph = PasswordHasher(time_cost=3, memory_cost=65536, parallelism=4, hash_len=32, salt_len=16)

auth_bp = Blueprint('auth', __name__)

REQUIRED_FIELDS = [
    'username', 'email', 'password',
    'x25519_public_key', 'hpke_wrapped_private_key', 'argon2id_kek_salt',
]

@auth_bp.route('/register', methods=['POST'])
def register():
    data = request.get_json()

    if not data:
        return jsonify({'error': 'Request body must be JSON'}), 400

    missing = [f for f in REQUIRED_FIELDS if not data.get(f)]
    if missing:
        return jsonify({'error': f"Missing required fields: {', '.join(missing)}"}), 400

    password_hash = ph.hash(data['password'])
    # Hash format: $argon2id$v=19$m=...,t=...,p=...$<base64-salt>$<base64-hash>
    argon2id_salt = password_hash.split('$')[4]

    user_id = str(uuid.uuid4())
    now = datetime.now(timezone.utc)

    db = get_db()
    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO users
                (id, username, email, argon2id_server_hash, argon2id_server_salt,
                 x25519_public_key, hpke_wrapped_private_key, argon2id_kek_salt,
                 tofu_key_pinned_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                user_id, data['username'], data['email'],
                password_hash, argon2id_salt,
                data['x25519_public_key'], data['hpke_wrapped_private_key'],
                data['argon2id_kek_salt'], now,
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
