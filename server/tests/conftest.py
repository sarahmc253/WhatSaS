import os
import pytest

# Provide dummy env vars so create_app() passes its startup check.
# The DB is never actually connected — all DB calls in tests are mocked.
for _key, _val in {
    'DB_HOST': '127.0.0.1',
    'DB_PORT': '3306',
    'DB_USER': 'test',
    'DB_PASSWORD': 'test',
    'DB_NAME': 'test_db',
    'JWT_SECRET_KEY': 'test-secret-key-that-is-long-enough-for-hmac-sha256',
}.items():
    os.environ.setdefault(_key, _val)

from server.app import create_app  # noqa: E402


@pytest.fixture
def app():
    flask_app = create_app()
    flask_app.config['TESTING'] = True
    return flask_app


@pytest.fixture
def client(app):
    return app.test_client()
