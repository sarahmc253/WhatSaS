from flask import Blueprint, jsonify, request
from flask_jwt_extended import jwt_required, get_jwt_identity

from .. import get_db

users_bp = Blueprint('users', __name__)



@users_bp.route('/<username>', methods=['GET'])
@jwt_required()
def get_user(username):
    db = get_db()
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            'SELECT id, username, x25519_public_key FROM users WHERE username = %s',
            (username,),
        )
        user = cursor.fetchone()
    finally:
        cursor.close()

    if user is None:
        return jsonify({'error': 'User not found'}), 404

    return jsonify(user), 200
