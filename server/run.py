import os
from dotenv import load_dotenv
from app import create_app

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

app = create_app()

if __name__ == '__main__':
    host = os.getenv('FLASK_HOST', '127.0.0.1')
    debug = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'
    if host == '0.0.0.0':
        debug = False
    ssl_cert = os.getenv('SSL_CERT', '/home/student/server.crt')
    ssl_key = os.getenv('SSL_KEY', '/home/student/server.key')
    app.run(host=host, debug=debug, ssl_context=(ssl_cert, ssl_key))

