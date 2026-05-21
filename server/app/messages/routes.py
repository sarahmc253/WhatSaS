from flask import Blueprint, jsonify
from flask_jwt_extended import jwt_required, get_jwt_identity

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

@messages_bp.route('/messages/<string:id>', methods=['DELETE'])
@jwt_required()
def delete_message(id):
    return jsonify({'message': f'message {id} deleted'}), 200

@messages_bp.route('/messages/<string:id>/forward', methods=['POST'])
@jwt_required()
def forward_message(id):
    return jsonify({'message': f'message {id} forwarded'}), 200

@messages_bp.route('/messages/<string:id>/revoke', methods=['POST'])
@jwt_required()
def revoke_message(id):
    return jsonify({'message': f'message {id} revoked'}), 200
