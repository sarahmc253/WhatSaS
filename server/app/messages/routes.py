import re
import uuid
from datetime import datetime, timezone

import mysql.connector
from flask import Blueprint, request, jsonify
from flask_jwt_extended import jwt_required, get_jwt_identity

from .. import get_db

messages_bp = Blueprint('messages', __name__)

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
    return jsonify({'message': 'message sent'}), 200

@messages_bp.route('/messages/<string:message_id>', methods=['DELETE'])
@jwt_required()
def delete_message(message_id):
    return jsonify({'message': f'message {message_id} deleted'}), 200

@messages_bp.route('/messages/<string:message_id>/forward', methods=['POST'])
@jwt_required()
def forward_message(message_id):
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, ['recipientUsername', 'ciphertext', 'nonce', 'content_hash'])
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    if not _HEX32_RE.match(data['content_hash']):
        return jsonify({'error': 'content_hash must be a 64-character hex string (keccak256)'}), 400

    current_user_id = get_jwt_identity()
    db = get_db()

    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute('SELECT sender_id FROM messages WHERE id = %s', (message_id,))
        message = cursor.fetchone()
    finally:
        cursor.close()

    if message is None:
        return jsonify({'error': 'Message not found'}), 404

    if message['sender_id'] != current_user_id:
        return jsonify({'error': 'Forbidden'}), 403

    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute('SELECT id FROM users WHERE username = %s', (data['recipientUsername'],))
        recipient = cursor.fetchone()
    finally:
        cursor.close()

    if recipient is None:
        return jsonify({'error': 'Recipient not found'}), 404

    content_hash = data['content_hash'] if data['content_hash'].startswith('0x') else '0x' + data['content_hash']
    new_id = str(uuid.uuid4())
    now = datetime.now(timezone.utc)

    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, content_hash, created_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            """,
            (
                new_id, current_user_id, recipient['id'],
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

    return jsonify({'id': new_id}), 201

@messages_bp.route('/messages/<string:message_id>/revoke', methods=['POST'])
@jwt_required()
def revoke_message(message_id):
    return jsonify({'message': f'message {message_id} revoked'}), 200
