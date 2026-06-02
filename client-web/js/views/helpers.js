// Shared utilities used by auth.js and inbox.js

// ── XSS escaping ──────────────────────────────────────────────────────────
export function esc(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#x27;');
}

// ── Date formatting ───────────────────────────────────────────────────────
export function formatDate(ts) {
    if (!ts) return '';
    const d = new Date(typeof ts === 'number' ? ts * 1000 : ts);
    return isNaN(d) ? '' : d.toLocaleString();
}

// ── Byte helpers ──────────────────────────────────────────────────────────
export function hexToBytes(hex) {
    if (hex.length % 2 !== 0) throw new Error('hexToBytes: odd-length hex string');
    if (/[^0-9a-fA-F]/.test(hex)) throw new Error('hexToBytes: invalid hex character');
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) {
        bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    }
    return bytes;
}

// Decode hex or base64 string to Uint8Array (C++ sends base64; web sends hex).
export function decodeField(str) {
    if (/^[0-9a-f]+$/i.test(str) && str.length % 2 === 0) return hexToBytes(str);
    return Uint8Array.from(atob(str), c => c.charCodeAt(0));
}

// Parse ISO-8601 or RFC 2822 date string to Unix seconds integer.
export function parseTimestamp(str) {
    if (!str) return 0;
    const d = new Date(str.includes('T') || str.includes(',') ? str : str.replace(' ', 'T') + 'Z');
    return isNaN(d) ? 0 : Math.floor(d.getTime() / 1000);
}

// ── Crypto helpers ────────────────────────────────────────────────────────

// SHA-256 fingerprint of a raw public key — first 8 bytes, colon-separated uppercase hex.
// Matches the C++ client's format for out-of-band identity verification.
export async function keyFingerprint(pkBytes) {
    const hash = await crypto.subtle.digest('SHA-256', pkBytes);
    return Array.from(new Uint8Array(hash).slice(0, 8))
        .map(b => b.toString(16).padStart(2, '0').toUpperCase())
        .join(':');
}

// ── TOFU key pinning ──────────────────────────────────────────────────────
// First contact pins the key in localStorage. Subsequent contacts warn if it changed.
export const TOFU_PREFIX = 'tofu_pk_';

export function tofuCheck(username, b64PublicKey) {
    const stored = localStorage.getItem(TOFU_PREFIX + username);
    if (!stored) {
        localStorage.setItem(TOFU_PREFIX + username, b64PublicKey);
        return { pinned: true, changed: false };
    }
    if (stored !== b64PublicKey) {
        return { pinned: false, changed: true, storedKey: stored };
    }
    return { pinned: false, changed: false };
}

// ── Password strength validation ──────────────────────────────────────────
export function validatePassword(pw) {
    const errors = [];
    if (pw.length < 8)             errors.push('at least 8 characters');
    if (!/[A-Z]/.test(pw))         errors.push('one uppercase letter');
    if (!/[a-z]/.test(pw))         errors.push('one lowercase letter');
    if (!/[0-9]/.test(pw))         errors.push('one number');
    if (!/[^A-Za-z0-9]/.test(pw))  errors.push('one special character');
    return errors;
}

// ── Inline error toast ────────────────────────────────────────────────────
export function showInlineError(container, text) {
    const el = document.createElement('div');
    el.className = 'error-msg';
    el.textContent = text;
    container.prepend(el);
    setTimeout(() => el.remove(), 4000);
}
