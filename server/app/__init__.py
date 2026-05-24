import os
import mysql.connector
from datetime import timedelta
from flask import Flask, g, current_app
from flask_jwt_extended import JWTManager

def get_db():
    if 'db' not in g:
        g.db = mysql.connector.connect(
            host=current_app.config['DB_HOST'],
            port=current_app.config['DB_PORT'],
            user=current_app.config['DB_USER'],
            password=current_app.config['DB_PASSWORD'],
            database=current_app.config['DB_NAME']
        )
    return g.db

def create_app():
    app = Flask(__name__)

    required = ['DB_HOST', 'DB_PORT', 'DB_USER', 'DB_PASSWORD', 'DB_NAME', 'JWT_SECRET_KEY']
    missing = [key for key in required if not os.getenv(key)]
    if missing:
        raise RuntimeError(f"Missing required environment variables: {', '.join(missing)}")

    app.config['DB_HOST'] = os.getenv('DB_HOST')
    try:
        app.config['DB_PORT'] = int(os.getenv('DB_PORT'))
    except ValueError as err:
        raise RuntimeError(
            f"DB_PORT must be a valid integer, got: {os.getenv('DB_PORT')}"
        ) from err
    app.config['DB_USER'] = os.getenv('DB_USER')
    app.config['DB_PASSWORD'] = os.getenv('DB_PASSWORD')
    app.config['DB_NAME'] = os.getenv('DB_NAME')
    app.config['SECRET_KEY'] = os.getenv('JWT_SECRET_KEY')
    app.config['JWT_SECRET_KEY'] = os.getenv('JWT_SECRET_KEY')
    # 15 minutes balances security and usability: short enough to limit exposure if a token
    # is stolen, at the cost of requiring re-login for long sessions. A refresh token endpoint
    # would remove that UX penalty, but adds scope — chosen not to implement for now.
    app.config['JWT_ACCESS_TOKEN_EXPIRES'] = timedelta(minutes=15)

    JWTManager(app)

    @app.teardown_appcontext
    def close_db(e=None):
        db = g.pop('db', None)
        if db is not None and db.is_connected():
            db.close()

    from .auth.routes import auth_bp
    from .messages.routes import messages_bp
    app.register_blueprint(auth_bp, url_prefix='/auth')
    app.register_blueprint(messages_bp, url_prefix='')

    return app
