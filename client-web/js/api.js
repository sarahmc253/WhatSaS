/**
 * api.js — thin wrapper around fetch() for all backend calls.
 *
 * Token lifecycle:
 *   - Stored in sessionStorage so it clears when the tab is closed.
 *   - JWT expires in 15 min server-side; the server returns 401 when
 *     it does, which this module surfaces as a thrown Error.
 *
 * Private key lifecycle:
 *   - Unwrapped from wrapped_private_key at login time using the user's passphrase.
 *   - Held in a module-level variable only — never written to sessionStorage or localStorage.
 *   - Cleared on logout.
 */

import { decryptPrivateKey, EncryptedPrivateKey } from '../crypto/keyStorage.js';

const BASE_URL = window.location.origin;
const TOKEN_KEY = 'whatsas_token';

// ── Token helpers ─────────────────────────────────────────────────────────
function getToken()   { return sessionStorage.getItem(TOKEN_KEY); }
function setToken(t)  { sessionStorage.setItem(TOKEN_KEY, t); }
function clearToken() { sessionStorage.removeItem(TOKEN_KEY); }

export function isAuthenticated() { return !!getToken(); }

// ── Private key store (in-memory only, never persisted) ──────────────────
let _sessionPrivateKey = null;

export function setPrivateKey(key) { _sessionPrivateKey = key; }
export function getPrivateKey()    { return _sessionPrivateKey; }
export function clearPrivateKey()  { _sessionPrivateKey = null; }

// ── Core fetch wrapper ────────────────────────────────────────────────────
async function request(method, path, { body = null, auth = false } = {}) {
    const headers = { 'Content-Type': 'application/json' };

    if (auth) {
        const token = getToken();
        if (!token) throw new Error('Not authenticated');
        headers['Authorization'] = `Bearer ${token}`;
    }

    const opts = { method, headers };
    if (body !== null) opts.body = JSON.stringify(body);

    const res = await fetch(BASE_URL + path, opts);

    // 204 No Content — return early, nothing to parse
    if (res.status === 204) return null;

    const data = await res.json().catch(() => ({}));

    if (!res.ok) {
        const msg = data.message || data.error || `HTTP ${res.status}`;
        throw Object.assign(new Error(msg), { status: res.status, data });
    }

    return data;
}

// ── Auth ──────────────────────────────────────────────────────────────────
export async function login(username, password) {
    const data = await request('POST', '/auth/login', {
        body: { username, password }
    });
    if (!data.token) throw new Error('Login succeeded but no token was returned');
    setToken(data.token);

    if (data.wrapped_private_key) {
        try {
            const parsed    = JSON.parse(atob(data.wrapped_private_key));
            const encrypted = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes = await decryptPrivateKey(encrypted, password);
            const privKey   = await crypto.subtle.importKey(
                'raw', privBytes, { name: 'X25519' }, false, ['deriveKey', 'deriveBits'],
            );
            setPrivateKey(privKey);
        } catch {
            // Placeholder key material (test users) or corrupted key — login still succeeds,
            // inbox will see getPrivateKey() === null and skip decryption.
        }
    }

    // Return full payload — caller may need key material for E2E crypto
    return data;
}

export async function register(username, email, password, cryptoPayload) {
    const { x25519_public_key, wrapped_private_key, kek_salt } = cryptoPayload ?? {};
    if (!x25519_public_key || !wrapped_private_key || !kek_salt) {
        throw new Error('Registration blocked: E2E crypto material is not yet implemented');
    }
    return request('POST', '/auth/register', {
        body: { username, email, password, ...cryptoPayload }
    });
}

export function logout() {
    const token = getToken();
    if (token) {
        fetch(`${BASE_URL}/auth/logout`, {
            method: 'POST',
            headers: { 'Authorization': `Bearer ${token}` },
        }).catch(() => {});
    }
    clearToken();
    clearPrivateKey();
}

// ── Messages ──────────────────────────────────────────────────────────────
export function getMessages() {
    return request('GET', '/messages', { auth: true });
}

export function sendMessage(payload) {
    return request('POST', '/messages', { body: payload, auth: true });
}

export function deleteMessage(id) {
    return request('DELETE', `/messages/${id}`, { auth: true });
}

export function forwardMessage(id) {
    return request('POST', `/messages/${id}/forward`, { auth: true });
}

export function revokeMessage(id) {
    return request('POST', `/messages/${id}/revoke`, { auth: true });
}
