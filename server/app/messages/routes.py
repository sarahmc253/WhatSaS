import hashlib
import logging
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
logger = logging.getLogger(__name__)

SEND_FIELDS = ['recipient_id', 'ciphertext', 'nonce', 'ephemeral_pk']

def _invalid_fields(data, fields):
    return [
        f for f in fields
        if not isinstance(data.get(f), str) or not data[f].strip()
    ]

@messages_bp.route('/messages', methods=['GET'])
@jwt_required()
def get_messages():
    current_user_id = get_jwt_identity()
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            """
            SELECT m.id, m.sender_id, u.username AS sender_username,
                   u.x25519_public_key AS sender_x25519_public_key,
                   m.ciphertext, m.nonce, m.ephemeral_pk, m.created_at,
                   ru.username AS recipient_username,
                   'received' AS direction
            FROM messages m
            JOIN users u  ON u.id = m.sender_id
            JOIN users ru ON ru.id = m.recipient_id
            WHERE m.recipient_id = %s AND m.is_revoked = 0

            UNION ALL

            SELECT m.id, m.sender_id, u.username AS sender_username,
                   u.x25519_public_key AS sender_x25519_public_key,
                   m.ciphertext, m.nonce, m.ephemeral_pk, m.created_at,
                   ru.username AS recipient_username,
                   'sent' AS direction
            FROM messages m
            JOIN users u  ON u.id = m.sender_id
            JOIN users ru ON ru.id = m.recipient_id
            WHERE m.sender_id = %s AND m.is_revoked = 0

            ORDER BY created_at ASC
            """,
            (current_user_id, current_user_id),
        )
        rows = cursor.fetchall()
    finally:
        cursor.close()
    return jsonify({'messages': rows}), 200

@messages_bp.route('/messages', methods=['POST'])
@jwt_required()
def send_message():
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, SEND_FIELDS)
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    # Accept a client-generated message_id (hex string, 32 chars) for AD reconstruction.
    # Validate format strictly to prevent injection; fall back to server-generated UUID.
    client_msg_id = data.get('message_id', '')
    if isinstance(client_msg_id, str) and re.fullmatch(r'[0-9a-f]{32}', client_msg_id):
        message_id = client_msg_id
    else:
        message_id = str(uuid.uuid4()).replace('-', '')
    sender_id = get_jwt_identity()
    now = datetime.now(timezone.utc)
    content_hash = '0x' + hashlib.sha256(data['ciphertext'].encode('utf-8')).hexdigest()

    db = get_db()
    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, ephemeral_pk, created_at, content_hash)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                message_id, sender_id, data['recipient_id'],
                data['ciphertext'], data['nonce'], data['ephemeral_pk'], now, content_hash,
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
    if not current_app.config.get('ANCHORING_ENABLED'):
        return jsonify({'error': 'Anchoring is not enabled'}), 503
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

@messages_bp.route('/messages/<string:message_id>', methods=['GET'])
@jwt_required()
def get_message(message_id):
    current_user_id = get_jwt_identity()
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            """
            SELECT m.sender_id, m.recipient_id, m.ciphertext, m.nonce, m.ephemeral_pk,
                   m.created_at, u.x25519_public_key AS sender_x25519_public_key
            FROM messages m
            JOIN users u ON u.id = m.sender_id
            WHERE m.id = %s
            """,
            (message_id,),
        )
        message = cursor.fetchone()
    finally:
        cursor.close()
    if message is None:
        return jsonify({'error': 'Message not found'}), 404
    if message['sender_id'] != current_user_id and message['recipient_id'] != current_user_id:
        return jsonify({'error': 'Forbidden'}), 403
    return jsonify({
        'id': message_id,
        'sender_id': message['sender_id'],
        'recipient_id': message['recipient_id'],
        'ciphertext': message['ciphertext'],
        'nonce': message['nonce'],
        'ephemeral_pk': message['ephemeral_pk'],
        'sender_x25519_public_key': message['sender_x25519_public_key'],
        'created_at': message['created_at'].isoformat() if hasattr(message['created_at'], 'isoformat') else str(message['created_at']),
    }), 200

_DELETE_FLAG = {
    'sender': 'UPDATE messages SET is_deleted_sender = 1 WHERE id = %s',
    'recipient': 'UPDATE messages SET is_deleted_recipient = 1 WHERE id = %s',
}

@messages_bp.route('/messages/<string:message_id>', methods=['DELETE'])
@jwt_required()
def delete_message(message_id):
    current_user_id = get_jwt_identity()
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            'SELECT sender_id, recipient_id FROM messages WHERE id = %s',
            (message_id,),
        )
        message = cursor.fetchone()
    finally:
        cursor.close()
    if message is None:
        return jsonify({'error': 'Message not found'}), 404
    if message['sender_id'] != current_user_id and message['recipient_id'] != current_user_id:
        return jsonify({'error': 'Forbidden'}), 403
    role = 'sender' if message['sender_id'] == current_user_id else 'recipient'
    sql = _DELETE_FLAG[role]
    cursor = db.cursor()
    try:
        cursor.execute(sql, (message_id,))
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        cursor.close()
    return jsonify({'message': f'message {message_id} deleted'}), 200

@messages_bp.route('/messages/<string:message_id>/forward', methods=['POST'])
@jwt_required()
def forward_message(message_id):
    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({'error': 'Request body must be a JSON object'}), 400

    invalid = _invalid_fields(data, ['recipientUsername', 'ciphertext', 'nonce', 'ephemeral_pk'])
    if invalid:
        return jsonify({'error': f"Missing or invalid fields: {', '.join(invalid)}"}), 400

    current_user_id = get_jwt_identity()
    db = get_db()

    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute('SELECT sender_id, recipient_id FROM messages WHERE id = %s', (message_id,))
        message = cursor.fetchone()
    finally:
        cursor.close()

    if message is None:
        return jsonify({'error': 'Message not found'}), 404

    if message['sender_id'] != current_user_id and message['recipient_id'] != current_user_id:
        return jsonify({'error': 'Forbidden'}), 403

    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute('SELECT id FROM users WHERE username = %s', (data['recipientUsername'],))
        recipient = cursor.fetchone()
    finally:
        cursor.close()

    if recipient is None:
        return jsonify({'error': 'Recipient not found'}), 404

    new_id = str(uuid.uuid4())
    now = datetime.now(timezone.utc)
    content_hash = '0x' + hashlib.sha256(data['ciphertext'].encode('utf-8')).hexdigest()

    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, ephemeral_pk, created_at, original_message_id, content_hash)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                new_id, current_user_id, recipient['id'],
                data['ciphertext'], data['nonce'], data['ephemeral_pk'], now, message_id, content_hash,
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

@messages_bp.route('/blockchain-record', methods=['GET'])
@jwt_required()
def get_blockchain_record():
    tx_hash = request.args.get('tx_hash', '').strip()

    if not re.fullmatch(r'0x[0-9a-fA-F]{64}', tx_hash):
        return jsonify({'error': 'Invalid tx_hash — must be a 66-character 0x-prefixed hex string'}), 400

    db = get_db()
    try:
        cursor = db.cursor(dictionary=True)
        try:
            cursor.execute(
                'SELECT merkle_root, block_timestamp FROM blockchain_records WHERE tx_hash = %s',
                (tx_hash,),
            )
            record = cursor.fetchone()
        finally:
            cursor.close()
    except Exception:
        logger.exception('DB error in get_blockchain_record for tx_hash=%s', tx_hash)
        return jsonify({'error': 'Database error — please try again'}), 500

    if record is None:
        return jsonify({'error': 'No blockchain record found for this transaction hash'}), 404

    return jsonify({
        'merkle_root': record['merkle_root'],
        'block_timestamp': record['block_timestamp'].isoformat() if record['block_timestamp'] else None,
    }), 200


@messages_bp.route('/messages/<string:message_id>/revoke', methods=['POST'])
@jwt_required()
def revoke_message(message_id):
    current_user_id = get_jwt_identity()
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            """
            SELECT id, sender_id FROM messages
            WHERE id = %s AND original_message_id IS NOT NULL
            """,
            (message_id,),
        )
        message = cursor.fetchone()
    finally:
        cursor.close()

    if message is None:
        return jsonify({'error': 'Forwarded message not found'}), 404

    if message['sender_id'] != current_user_id:
        return jsonify({'error': 'Forbidden'}), 403

    now = datetime.now(timezone.utc)
    cursor = db.cursor()
    try:
        cursor.execute(
            "UPDATE messages SET is_revoked = 1 WHERE id = %s",
            (message_id,),
        )
        cursor.execute(
            "UPDATE message_access SET revoked_at = %s WHERE message_id = %s",
            (now, message_id),
        )
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        cursor.close()

    return jsonify({'message': f'message {message_id} revoked'}), 200
