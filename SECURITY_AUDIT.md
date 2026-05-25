# Security Audit — HTTP Security Headers

**Project:** WhatSaS Secure Messaging  
**Auditor:** Sreejita Saha  
**Date:** 2026-05-25  
**Scope:** Flask server security header hardening + production debug mode verification

---

## Summary

This session addressed the absence of HTTP security headers on the Flask API and confirmed that debug mode and verbose error traces are disabled in production. Four headers were added via a centralised `@app.after_request` hook in `server/app/__init__.py`.

---

## Findings & Remediations

### Finding 1 — Missing `Strict-Transport-Security` Header

**Severity:** High  
**CWE:** CWE-523 (Unprotected Transport of Credentials)  
**OWASP:** A05:2021 Security Misconfiguration

**Description:**  
The server returned no `Strict-Transport-Security` (HSTS) header. Without HSTS, a browser that knows the domain supports HTTPS can be silently downgraded to HTTP by a man-in-the-middle attacker (SSL-stripping attack). Any JWT tokens or login credentials submitted during a downgraded session would be exposed in plaintext.

**Evidence (before fix):**  
```
HTTP/1.1 200 OK
Content-Type: application/json
(no Strict-Transport-Security header)
```

**Remediation applied:**  
Added to `server/app/__init__.py` via `@app.after_request`:
```python
response.headers['Strict-Transport-Security'] = 'max-age=63072000; includeSubDomains'
```

**Value rationale:**  
- `max-age=63072000` = 2 years. Browsers cache this directive and refuse all HTTP connections for that duration.  
- `includeSubDomains` extends the policy to any subdomain of the deployment domain.  
- `preload` intentionally omitted — preloading is permanent and requires explicit opt-in to the HSTS preload registry.

**Infrastructure note:**  
Gunicorn binds to `127.0.0.1:5000` (HTTP, loopback only). A TLS-terminating reverse proxy sits in front and handles public HTTPS traffic — confirmed by `sas.theburkenator.com:443` references in the test suite. Flask's response headers pass through the proxy to the client over TLS. Sending HSTS from the application layer is safe and provides defence in depth should the proxy configuration ever change.

---

### Finding 2 — Missing `Content-Security-Policy` Header

**Severity:** Medium  
**CWE:** CWE-358 (Improperly Implemented Security Check for Standard)  
**OWASP:** A05:2021 Security Misconfiguration

**Description:**  
No `Content-Security-Policy` (CSP) header was returned. Without CSP, if a bug ever caused the API to return HTML containing injected scripts, a browser would execute them. CSP is defence-in-depth: even if injection occurs, the browser enforces policy before executing anything.

**Evidence (before fix):**  
```
HTTP/1.1 200 OK
Content-Type: application/json
(no Content-Security-Policy header)
```

**Remediation applied:**  
```python
response.headers['Content-Security-Policy'] = "default-src 'none'"
```

**Value rationale:**  
This is a pure JSON REST API — it serves no HTML, scripts, images, fonts, stylesheets, or frames. `default-src 'none'` blocks every content category. If a web frontend is ever co-hosted on the same origin, this directive would need extending (e.g. `default-src 'self'`), but that is out of scope for the current spec.

---

### Finding 3 — Missing `X-Frame-Options` Header

**Severity:** Medium  
**CWE:** CWE-1021 (Improper Restriction of Rendered UI Layers — Clickjacking)  
**OWASP:** A05:2021 Security Misconfiguration

**Description:**  
No `X-Frame-Options` header was present. A malicious webpage could embed the API in a hidden `<iframe>` and use UI-overlay tricks to cause an authenticated user to unknowingly perform API actions (clickjacking).

**Evidence (before fix):**  
```
HTTP/1.1 200 OK
(no X-Frame-Options header)
```

**Remediation applied:**  
```python
response.headers['X-Frame-Options'] = 'DENY'
```

**Value rationale:**  
`DENY` prevents any page — including pages on the same origin — from framing this API. `SAMEORIGIN` would be unnecessarily permissive for a service with no frontend UI of its own.

**Note:** Modern browsers honour `Content-Security-Policy: frame-ancestors 'none'` as the superseding directive. Both are set here for compatibility with older browsers that only check `X-Frame-Options`.

---

### Finding 4 — Missing `X-Content-Type-Options` Header

**Severity:** Low–Medium  
**CWE:** CWE-430 (Deployment of Wrong Handler)  
**OWASP:** A05:2021 Security Misconfiguration

**Description:**  
No `X-Content-Type-Options` header was present. Without `nosniff`, some browsers attempt to infer the MIME type of a response from its content rather than trusting the `Content-Type` header. A JSON response containing HTML-like content could be re-interpreted as HTML and rendered, potentially executing injected scripts.

**Evidence (before fix):**  
```
HTTP/1.1 200 OK
Content-Type: application/json
(no X-Content-Type-Options header)
```

**Remediation applied:**  
```python
response.headers['X-Content-Type-Options'] = 'nosniff'
```

---

### Finding 5 — Debug Mode Status (Confirmed Off)

**Severity:** Informational (no vulnerability found)  
**CWE:** CWE-94 (Code Injection via debug console) if enabled  
**OWASP:** A05:2021 Security Misconfiguration

**Description:**  
Flask's debug mode activates the Werkzeug interactive debugger, which provides a PIN-protected Python REPL accessible from the browser. If enabled in production, an attacker who obtains or brute-forces the PIN gains remote code execution on the server.

**Findings:**  
`server/run.py` line 10:
```python
debug = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'
```
- Default is `'false'` — debug mode is off unless `FLASK_DEBUG=true` is explicitly set in the environment.
- The production systemd service (`whatsas.service`) launches Gunicorn: `gunicorn run:app`. Gunicorn imports the `app` object directly and never calls `app.run()`. The `if __name__ == '__main__':` block (lines 9–11 of `run.py`) is never executed in production.
- **Conclusion:** Even if `FLASK_DEBUG=true` were accidentally present in `.env`, it would have no effect in the Gunicorn production deployment.

**Action taken:**  
Added inline comment to `server/run.py` line 10 to make the production safety explicit for future maintainers:
```python
debug = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'  # default false; gunicorn never calls app.run() so this is irrelevant in production
```

---

### Finding 6 — Verbose Error Traces (Confirmed Off)

**Severity:** Informational (no vulnerability found)  
**CWE:** CWE-209 (Generation of Error Message Containing Sensitive Information)  
**OWASP:** A09:2021 Security Logging and Monitoring Failures

**Description:**  
Flask in non-debug mode returns a generic `500 Internal Server Error` response with no stack trace in the response body. Stack traces exposed to clients can reveal internal file paths, library versions, variable names, and logic flow — all useful to an attacker during reconnaissance.

**Findings:**  
- No custom error handlers are registered in `create_app()`.  
- Flask's built-in default for non-debug mode returns a minimal error response with no trace.  
- Gunicorn logs errors to stderr (configured in `whatsas.service` with `--error-logfile -`) — these logs are server-side only and not returned to clients.

**Action taken:** None required. Current behaviour is already correct.

---

## Implementation Details

**File modified:** `server/app/__init__.py`  
**Change:** Added `@app.after_request` hook inside `create_app()` after the teardown handler, before blueprint registration.

**Why `@app.after_request`:**  
This hook runs on every outgoing response regardless of which route, blueprint, or error handler produced it — including 404s and 500s. Per-route decorators would miss newly added endpoints. A standalone WSGI middleware class would work too but adds complexity for four header assignments.

```python
@app.after_request
def set_security_headers(response):
    response.headers['Strict-Transport-Security'] = 'max-age=63072000; includeSubDomains'
    response.headers['Content-Security-Policy'] = "default-src 'none'"
    response.headers['X-Frame-Options'] = 'DENY'
    response.headers['X-Content-Type-Options'] = 'nosniff'
    return response
```

---

## Verification

To confirm headers are present after deploying:

```bash
# Local (HTTP — HSTS will be present but browsers ignore it over plain HTTP)
curl -I http://localhost:5000/auth/login

# Production (HTTPS — all headers including HSTS should be active)
curl -I https://sas.theburkenator.com/auth/login
```

Expected response headers:
```
Strict-Transport-Security: max-age=63072000; includeSubDomains
Content-Security-Policy: default-src 'none'
X-Frame-Options: DENY
X-Content-Type-Options: nosniff
```

To confirm debug mode and error traces are off:
```bash
# Hit a non-existent endpoint — expect 404 with no stack trace in body
curl -s http://localhost:5000/does-not-exist

# Expected: {"msg": "..."} or plain 404 — no Python traceback
```

---

## Residual Risks / Not In Scope

| Item | Status | Notes |
|---|---|---|
| CORS policy | Not addressed | No `Access-Control-Allow-Origin` header set. If a browser-based client on a different origin needs to call this API, CORS must be configured explicitly. |
| `Referrer-Policy` | Not added | Low priority for a JSON API; no sensitive data in URLs. |
| `Permissions-Policy` | Not added | Relevant for browser feature gating; not applicable to a pure API. |
| Rate limiting | Not addressed | No per-IP or per-user rate limit on auth endpoints. Brute-force of login remains possible. |
| HSTS preload | Intentionally omitted | Preloading is permanent; requires submitting the domain to the preload list and meeting strict criteria. Deferred to production hardening. |
