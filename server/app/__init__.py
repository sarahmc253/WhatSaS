import json
import logging
import os
import mysql.connector
from datetime import timedelta
from flask import Flask, g, current_app, request
from flask_jwt_extended import JWTManager
from apscheduler.schedulers.background import BackgroundScheduler

logger = logging.getLogger(__name__)

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

def log_audit(action, user_id=None, message_id=None, metadata=None):
    try:
        db = get_db()
        cur = db.cursor()
        ip = request.remote_addr
        meta_str = json.dumps(metadata) if metadata else None
        cur.execute(
            "INSERT INTO audit_log (user_id, message_id, action, ip_address, metadata) "
            "VALUES (%s, %s, %s, %s, %s)",
            (user_id, message_id, action, ip, meta_str)
        )
        db.commit()
    except Exception as e:
        current_app.logger.error("audit log failed [%s]: %s", action, e)


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
    # 1 hour balances security and usability: short enough to limit exposure if a token
    # is stolen, while covering a typical user session without forced re-login. A refresh
    # token endpoint would allow indefinite sessions, but adds scope — not implemented.
    app.config['JWT_ACCESS_TOKEN_EXPIRES'] = timedelta(hours=1)

    web3_vars = ['WEB3_RPC_URL', 'CONTRACT_ADDRESS', 'WALLET_PRIVATE_KEY']
    missing_web3 = [v for v in web3_vars if not os.getenv(v)]
    if missing_web3:
        logger.warning('Blockchain anchoring disabled — missing env vars: %s', ', '.join(missing_web3))
        app.config['ANCHORING_ENABLED'] = False
    else:
        app.config['ANCHORING_ENABLED'] = True
        app.config['WEB3_RPC_URL'] = os.getenv('WEB3_RPC_URL')
        app.config['CONTRACT_ADDRESS'] = os.getenv('CONTRACT_ADDRESS')
        app.config['WALLET_PRIVATE_KEY'] = os.getenv('WALLET_PRIVATE_KEY')

    jwt = JWTManager(app)

    # In-memory JTI blocklist — tokens expire in 15 min so the set stays small.
    # Cleared on server restart, which is acceptable: a restarted server issues new tokens.
    _revoked_jtis: set = set()
    app.revoked_jtis = _revoked_jtis

    @jwt.token_in_blocklist_loader
    def check_if_token_revoked(jwt_header, jwt_payload):
        return jwt_payload['jti'] in app.revoked_jtis

    @app.teardown_appcontext
    def close_db(e=None):
        db = g.pop('db', None)
        if db is not None and db.is_connected():
            db.close()

    @app.after_request
    def set_security_headers(response):
        # Tell browsers to only contact this server over HTTPS for 2 years.
        # Safe to send from Flask — Gunicorn passes it through the TLS-terminating proxy.
        response.headers['Strict-Transport-Security'] = 'max-age=63072000; includeSubDomains'
        # Pure JSON API: block all content types (scripts, frames, images) by default.
        response.headers['Content-Security-Policy'] = "default-src 'none'; frame-ancestors 'none'"
        # Prevent this API being embedded in any iframe (clickjacking mitigation).
        response.headers['X-Frame-Options'] = 'DENY'
        # Prevent browsers MIME-sniffing a JSON response as HTML/script.
        response.headers['X-Content-Type-Options'] = 'nosniff'
        return response
    
        

    from .auth.routes import auth_bp
    from .messages.routes import messages_bp
    from .users.routes import users_bp
    from .audit.routes import audit_bp
    app.register_blueprint(auth_bp, url_prefix='/auth')
    app.register_blueprint(messages_bp, url_prefix='')
    app.register_blueprint(users_bp, url_prefix='/users')
    app.register_blueprint(audit_bp, url_prefix='')

    if app.config['ANCHORING_ENABLED']:
        # In debug mode the reloader forks a child process; only start the scheduler
        # in the child (WERKZEUG_RUN_MAIN=true) to avoid two schedulers running.
        if not app.debug or os.environ.get('WERKZEUG_RUN_MAIN') == 'true':
            from .messages.anchor import anchor_pending, confirm_pending

            def _scheduled_anchor():
                with app.app_context():
                    anchor_pending()

            def _scheduled_confirm():
                with app.app_context():
                    confirm_pending()

            scheduler = BackgroundScheduler()
            scheduler.add_job(_scheduled_anchor, 'interval', minutes=5)
            scheduler.add_job(_scheduled_confirm, 'interval', minutes=2)
            scheduler.start()
            app.scheduler = scheduler

    return app
