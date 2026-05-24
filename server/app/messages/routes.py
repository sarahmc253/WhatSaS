from flask import Blueprint, jsonify
from flask_jwt_extended import jwt_required, get_jwt_identity
from datetime import datetime, timezone
from .. import get_db

messages_bp = Blueprint('messages', __name__)

@messages_bp.route('/messages', methods=['GET'])
@jwt_required()
def get_messages():
    current_user_id = get_jwt_identity()
    return jsonify({'user_id': current_user_id, 'messages': []}), 200

@messages_bp.route('/messages', methods=['POST'])
@jwt_required()
def send_message():
    return jsonify({'message': 'message sent'}), 200

@messages_bp.route('/messages/<string:message_id>', methods=['GET'])
@jwt_required()
def get_message(message_id):
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
    return jsonify({'id': message_id}), 200

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
