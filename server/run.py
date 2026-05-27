import os
from dotenv import load_dotenv
from app import create_app

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

app = create_app()

if __name__ == '__main__':
    debug = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'  # default false; gunicorn never calls app.run() so this is irrelevant in production
    app.run(debug=debug)
