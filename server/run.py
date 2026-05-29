import os
from dotenv import load_dotenv
from app import create_app

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

app = create_app()

if __name__ == '__main__':
    debug = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'
    app.run(host='0.0.0.0', port=2200, debug=debug, ssl_context=(
        '/home/student/server.crt',
        '/home/student/server.key'
    ))
