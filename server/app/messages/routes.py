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
                   m.ciphertext, m.nonce, m.ephemeral_pk, m.created_at
            FROM messages m
            JOIN users u ON u.id = m.sender_id
            WHERE m.recipient_id = %s AND m.is_revoked = 0
            """,
            (current_user_id,),
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

    message_id = str(uuid.uuid4())
    sender_id = get_jwt_identity()
    now = datetime.now(timezone.utc)

    db = get_db()
    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, ephemeral_pk, created_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            """,
            (
                message_id, sender_id, data['recipient_id'],
                data['ciphertext'], data['nonce'], data['ephemeral_pk'], now,
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
            'SELECT sender_id, recipient_id, ciphertext, nonce, ephemeral_pk FROM messages WHERE id = %s',
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
        'ciphertext': message['ciphertext'],
        'nonce': message['nonce'],
        'ephemeral_pk': message['ephemeral_pk'],
    }), 200

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

    cursor = db.cursor()
    try:
        cursor.execute(
            """
            INSERT INTO messages (id, sender_id, recipient_id, ciphertext, nonce, ephemeral_pk, created_at, original_message_id)
            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """,
            (
                new_id, current_user_id, recipient['id'],
                data['ciphertext'], data['nonce'], data['ephemeral_pk'], now, message_id,
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
