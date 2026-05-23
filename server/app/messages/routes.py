import re
import threading
import uuid
from datetime import datetime, timezone

import mysql.connector
from flask import Blueprint, current_app, request, jsonify
from flask_jwt_extended import jwt_required, get_jwt_identity

from .. import get_db
from .anchor import anchor_pending

messages_bp = Blueprint('messages', __name__)

SEND_FIELDS = ['recipient_id', 'ciphertext', 'nonce', 'content_hash']

_HEX32_RE = re.compile(r'^(0x)?[0-9a-fA-F]{64}$')

def _invalid_fields(data, fields):
    return [
        f for f in fields
        if not isinstance(data.get(f), str) or not data[f].strip()
    ]

@messages_bp.route('/messages', methods=['GET'])
@jwt_required()
def get_messages():
    current_user_id = get_jwt_identity()
    return jsonify({'user_id': current_user_id, 'messages': []}), 200

@messages_bp.route('/messages', methods=['POST'])
@jwt_required()
def send_message():
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, SEND_FIELDS)
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    if not _HEX32_RE.match(data['content_hash']):
        return jsonify({'error': 'content_hash must be a 64-character hex string (keccak256)'}), 400

    content_hash = data['content_hash'] if data['content_hash'].startswith('0x') else '0x' + data['content_hash']

    message_id = str(uuid.uuid4())
    sender_id = get_jwt_identity()
    now = datetime.now(timezone.utc)

    db = get_db()
    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, content_hash, created_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            """,
            (
                message_id, sender_id, data['recipient_id'],
                data['ciphertext'], data['nonce'], content_hash, now,
            ),
        )
        db.commit()
    except mysql.connector.IntegrityError as e:
        db.rollback()
        if e.errno == 1452:
            return jsonify({'error': 'Recipient not found'}), 404
        raise
    except Exception:
        db.rollback()
        raise
    finally:
        cursor.close()

    return jsonify({'id': message_id}), 201


@messages_bp.route('/flush', methods=['POST'])
@jwt_required()
def flush():
    user_id = get_jwt_identity()
    app = current_app._get_current_object()
    threading.Thread(
        target=lambda: _anchor_in_context(app, user_id),
        daemon=True,
    ).start()
    return jsonify({'message': 'Flush triggered'}), 202


def _anchor_in_context(app, user_id):
    with app.app_context():
        anchor_pending(user_id)

@messages_bp.route('/messages/<string:message_id>', methods=['DELETE'])
@jwt_required()
def delete_message(message_id):
    return jsonify({'message': f'message {message_id} deleted'}), 200

@messages_bp.route('/messages/<string:message_id>/forward', methods=['POST'])
@jwt_required()
def forward_message(message_id):
    return jsonify({'message': f'message {message_id} forwarded'}), 200

@messages_bp.route('/messages/<string:message_id>/revoke', methods=['POST'])
@jwt_required()
def revoke_message(message_id):
    return jsonify({'message': f'message {message_id} revoked'}), 200
