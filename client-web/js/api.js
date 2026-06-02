/**
 * api.js — thin wrapper around fetch() for all backend calls.
 *
 * Token lifecycle:
 *   - Stored in sessionStorage so it clears when the tab is closed.
 *   - JWT expires in 1 hour server-side; the server returns 401 when
 *     it does, which this module surfaces as a thrown Error.
 *
 * Private key lifecycle:
 *   - Unwrapped from wrapped_private_key at login time using the user's passphrase.
 *   - Held in a module-level variable only — never written to sessionStorage or localStorage.
 *   - Cleared on logout.
 */

import { decryptPrivateKey, EncryptedPrivateKey } from '../crypto/keyStorage.js';

const BASE_URL = window.location.origin;
const TOKEN_KEY      = 'whatsas_token';
const PUBLIC_KEY_KEY = 'whatsas_pubkey';
const WRAPPED_KEY_KEY = 'whatsas_wrapped';
const KEK_SALT_KEY   = 'whatsas_keksalt';
const USERNAME_KEY   = 'whatsas_username';
const USER_ID_KEY    = 'whatsas_userid';

// ── Token helpers ─────────────────────────────────────────────────────────
function getToken()   { return sessionStorage.getItem(TOKEN_KEY); }
function setToken(t)  { sessionStorage.setItem(TOKEN_KEY, t); }
function clearToken() { sessionStorage.removeItem(TOKEN_KEY); }

export function isAuthenticated() { return !!getToken(); }

// ── Private key store (in-memory only, never persisted) ──────────────────
// All other key material is mirrored to sessionStorage so it survives a
// page refresh within the same tab. The CryptoKey itself cannot be
// serialised (extractable:false), so it is re-derived from the persisted
// wrapped_private_key on the unlock screen after a reload.
let _sessionPrivateKey   = null;
let _sessionPublicKeyB64 = null;
let _sessionWrappedKey   = null;
let _sessionKekSalt      = null;
let _sessionUserId       = null;  // logged-in user's UUID — needed for AD in encryptMessage

export function setPrivateKey(key)   { _sessionPrivateKey   = key; }
export function getUserId()          { return _sessionUserId ?? sessionStorage.getItem(USER_ID_KEY); }
export function clearUserId()        { _sessionUserId = null; sessionStorage.removeItem(USER_ID_KEY); }
export function getPrivateKey()      { return _sessionPrivateKey; }
export function clearPrivateKey()    { _sessionPrivateKey   = null; }

export function getPublicKeyB64()    { return _sessionPublicKeyB64 ?? sessionStorage.getItem(PUBLIC_KEY_KEY); }
export function clearPublicKey()     { _sessionPublicKeyB64 = null; }

export function getUsername()        { return sessionStorage.getItem(USERNAME_KEY); }

export function getStoredWrappedKey() { return _sessionWrappedKey ?? sessionStorage.getItem(WRAPPED_KEY_KEY); }
export function getStoredKekSalt()    { return _sessionKekSalt    ?? sessionStorage.getItem(KEK_SALT_KEY); }

export function getWrappedKey()      { return getStoredWrappedKey(); }
export function getKekSalt()         { return getStoredKekSalt(); }

export function clearWrappedKey() {
    _sessionWrappedKey = null; _sessionKekSalt = null;
    sessionStorage.removeItem(WRAPPED_KEY_KEY);
    sessionStorage.removeItem(KEK_SALT_KEY);
}

export function setWrappedKey(wrapped, kekSalt) {
    _sessionWrappedKey = wrapped; _sessionKekSalt = kekSalt;
    sessionStorage.setItem(WRAPPED_KEY_KEY, wrapped);
    sessionStorage.setItem(KEK_SALT_KEY, kekSalt);
}

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
        console.error(`[api] ${method} ${path} → ${res.status}:`, data);
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
    if (data.x25519_public_key) {
        _sessionPublicKeyB64 = data.x25519_public_key;
        sessionStorage.setItem(PUBLIC_KEY_KEY, data.x25519_public_key);
    }
    if (data.wrapped_private_key) {
        _sessionWrappedKey = data.wrapped_private_key;
        sessionStorage.setItem(WRAPPED_KEY_KEY, data.wrapped_private_key);
    }
    if (data.kek_salt) {
        _sessionKekSalt = data.kek_salt;
        sessionStorage.setItem(KEK_SALT_KEY, data.kek_salt);
    }
    if (data.username) sessionStorage.setItem(USERNAME_KEY, data.username);
    if (data.user_id) {
        _sessionUserId = data.user_id;
        sessionStorage.setItem(USER_ID_KEY, data.user_id);
    }

    if (data.wrapped_private_key) {
        try {
            const parsed    = JSON.parse(atob(data.wrapped_private_key));
            const encrypted = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes = await decryptPrivateKey(encrypted, password);
            let privKey;
            try {
                privKey = await crypto.subtle.importKey(
                    'pkcs8', privBytes, { name: 'X25519' }, false, ['deriveBits'],
                );
            } catch (importErr) {
                console.error('importKey failed:', importErr.message, '| privBytes.length:', privBytes.length);
                throw importErr;
            }
            setPrivateKey(privKey);
        } catch (err) {
            console.error('[login] private key import failed — wrapped_private_key format mismatch or wrong password:', err);
        }
    }

    // Return full payload — caller may need key material for E2E crypto
    return data;
}

export async function register(username, password, cryptoPayload) {
    const { x25519_public_key, wrapped_private_key, kek_salt } = cryptoPayload ?? {};
    if (!x25519_public_key || !wrapped_private_key || !kek_salt) {
        throw new Error('Registration blocked: E2E crypto material is not yet implemented');
    }
    return request('POST', '/auth/register', {
        body: { username, password, ...cryptoPayload }
    });
}

export function changePassword(oldPassword, newPassword, newWrappedPrivateKey, newKekSalt) {
    return request('POST', '/auth/change-password', {
        body: { old_password: oldPassword, new_password: newPassword,
                wrapped_private_key: newWrappedPrivateKey, kek_salt: newKekSalt },
        auth: true,
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
    clearPublicKey();
    clearWrappedKey();
    clearUserId();
    sessionStorage.removeItem(PUBLIC_KEY_KEY);
    sessionStorage.removeItem(USERNAME_KEY);
}

// ── Messages ──────────────────────────────────────────────────────────────
export function getMessages() {
    return request('GET', '/messages', { auth: true });
}

// ── Users ─────────────────────────────────────────────────────────────────
export function getUser(username) {
    return request('GET', `/users/${encodeURIComponent(username)}`, { auth: true });
}

export function getMessage(id) {
    return request('GET', `/messages/${id}`, { auth: true });
}

export function sendMessage(payload) {
    return request('POST', '/messages', { body: payload, auth: true });
}

export function deleteMessage(id) {
    return request('DELETE', `/messages/${id}`, { auth: true });
}

export function forwardMessage(id, payload) {
    return request('POST', `/messages/${id}/forward`, { body: payload, auth: true });
}

export function revokeMessage(id) {
    return request('POST', `/messages/${id}/revoke`, { auth: true });
}

export function flushMessages() {
    return request('POST', '/flush', { auth: true });
}

export function triggerAnchor() {
    return request('POST', '/flush', { auth: true });
}
