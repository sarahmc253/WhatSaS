# Security Review — Auth & Endpoint Tests

Review prompts and manual curl tests run during this session.

---

## Session Prompts

1. List every endpoint in `server/app/` with its HTTP method and URL pattern.
2. Show only the login route in `server/app/auth/routes.py` — the username lookup, password check, and JWT creation lines only.
3. Confirm the dummy hash verify runs when `user is None` — verify the timing attack mitigation is in the right place and not after the early return.
4. Write curl commands to test broken auth on `/auth/login`: expired JWT on a protected endpoint, valid token used on another user's message, and a request with no token at all.

---

## Endpoint Map

```
POST   /auth/register
POST   /auth/login
POST   /auth/logout
GET    /messages
POST   /messages
GET    /messages/<message_id>
DELETE /messages/<message_id>
POST   /messages/<message_id>/forward
POST   /messages/<message_id>/revoke
POST   /flush
GET    /users/<username>
```

---

## Login Route — Key Lines

```python
# username lookup
cursor.execute("SELECT id, username, password_hash, ... FROM users WHERE username = %s", (data['username'],))
user = cursor.fetchone()

# password check
ph.verify(user['password_hash'], data['password'])

# JWT creation
token = create_access_token(identity=str(user['id']), additional_claims={'username': user['username']})
```

---

## Timing Attack Mitigation — Confirmed

`routes.py` lines 115–122: the dummy verify runs **inside** the `if user is None` block, before the early return. Mitigation is correctly placed.

```python
if user is None:
    try:
        ph.verify(_DUMMY_HASH, data['password'])
    except Exception:
        pass
    return jsonify({'error': 'Invalid credentials'}), 401
```

---

## Broken Auth curl Tests

### 1. Expired JWT on a protected endpoint

```bash
curl -X GET http://localhost:5000/messages \
  -H "Authorization: Bearer <EXPIRED_TOKEN>"
```

Expected: `401 Unauthorized`

---

### 2. Valid token used to access another user's message

```bash
curl -X GET http://localhost:5000/messages/<MESSAGE_ID> \
  -H "Authorization: Bearer <VALID_TOKEN_OTHER_USER>"
```

Expected: `403 Forbidden` or `404 Not Found` (must not return the message)

---

### 3. Request with no token at all

```bash
curl -X GET http://localhost:5000/messages \
  -H "Content-Type: application/json"
```

Expected: `401 Unauthorized`

---

### 4. Login with nonexistent username (username enumeration / timing check)

```bash
curl -X POST http://localhost:5000/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username": "nonexistent_user", "password": "wrongpassword"}'
```

Expected: `401 Unauthorized` with generic `"Invalid credentials"` — response time must be
indistinguishable from a valid-username/wrong-password attempt (dummy hash verify is in place).
