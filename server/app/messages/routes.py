from flask import Blueprint, jsonify

messages_bp = Blueprint('messages', __name__)

@messages_bp.route('/messages', methods=['GET'])
def get_messages():
    return jsonify({'message': 'get messages'}), 200

@messages_bp.route('/messages', methods=['POST'])
def send_message():
    return jsonify({'message': 'message sent'}), 200

@messages_bp.route('/messages/<string:id>', methods=['DELETE'])
def delete_message(id):
    return jsonify({'message': f'message {id} deleted'}), 200

@messages_bp.route('/messages/<string:id>/forward', methods=['POST'])
def forward_message(id):
    return jsonify({'message': f'message {id} forwarded'}), 200

@messages_bp.route('/messages/<string:id>/revoke', methods=['POST'])
def revoke_message(id):
    return jsonify({'message': f'message {id} revoked'}), 200
