from flask import Blueprint, jsonify, request
from flask_jwt_extended import jwt_required, get_jwt_identity

from .. import get_db

audit_bp = Blueprint('audit', __name__)

_VALID_ACTIONS = {
    'login', 'logout', 'send_message', 'read_message', 'edit_message',
    'revoke_message', 'delete_message', 'grant_access', 'revoke_access',
    'key_rotation', 'session_expired', 'failed_auth',
}

@audit_bp.route('/audit', methods=['GET'])
@jwt_required()
def get_audit_log():
    current_user_id = get_jwt_identity()

    action = request.args.get('action', '').strip() or None
    if action and action not in _VALID_ACTIONS:
        return jsonify({'error': f'Unknown action filter: {action}'}), 400

    try:
        limit = int(request.args.get('limit', 50))
        if limit < 1 or limit > 500:
            raise ValueError
    except ValueError:
        return jsonify({'error': 'limit must be an integer between 1 and 500'}), 400

    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        if action:
            cursor.execute(
                """
                SELECT id, user_id, message_id, action, ip_address, metadata, created_at
                FROM audit_log
                WHERE user_id = %s AND action = %s
                ORDER BY created_at DESC
                LIMIT %s
                """,
                (current_user_id, action, limit),
            )
        else:
            cursor.execute(
                """
                SELECT id, user_id, message_id, action, ip_address, metadata, created_at
                FROM audit_log
                WHERE user_id = %s
                ORDER BY created_at DESC
                LIMIT %s
                """,
                (current_user_id, limit),
            )
        rows = cursor.fetchall()
    finally:
        cursor.close()

    for row in rows:
        if hasattr(row.get('created_at'), 'isoformat'):
            row['created_at'] = row['created_at'].isoformat()

    return jsonify({'audit_log': rows}), 200
