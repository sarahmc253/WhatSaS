/**
 * views.js — render functions for Login, Inbox, and Compose views.
 *
 * Each function writes HTML into `container` (the #app element) then
 * wires up event listeners.  They never touch the DOM outside their
 * container, keeping routing concerns in app.js.
 */

import * as api from './api.js';
import { getUser, sendMessage, getUserId } from './api.js';
import { encryptMessage, decryptMessage } from '../crypto/messageEncryption.js';
import { generateKeypair, getPublicKeyBytes, getPrivateKeyBytes } from '../crypto/keypair.js';
import { encryptPrivateKey, decryptPrivateKey, EncryptedPrivateKey } from '../crypto/keyStorage.js';

// ── Helpers ───────────────────────────────────────────────────────────────

// Prevent XSS when interpolating any user-supplied string into innerHTML
function esc(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#x27;');
}

function formatDate(ts) {
    if (!ts) return '';
    // Accept both Unix seconds (number) and ISO strings
    const d = new Date(typeof ts === 'number' ? ts * 1000 : ts);
    return isNaN(d) ? '' : d.toLocaleString();
}

// ── Helpers (crypto) ─────────────────────────────────────────────────────
function hexToBytes(hex) {
    if (hex.length % 2 !== 0) throw new Error('hexToBytes: odd-length hex string');
    if (/[^0-9a-fA-F]/.test(hex)) throw new Error('hexToBytes: invalid hex character');
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) {
        bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    }
    return bytes;
}

// Decode hex or base64 string to Uint8Array (C++ sends base64 for ct/nonce; web sends hex).
function decodeField(str) {
    if (/^[0-9a-f]+$/i.test(str) && str.length % 2 === 0) return hexToBytes(str);
    return Uint8Array.from(atob(str), c => c.charCodeAt(0));
}

// Parse ISO-8601 or RFC 2822 date string to Unix seconds integer.
function parseTimestamp(str) {
    if (!str) return 0;
    const d = new Date(str.includes('T') || str.includes(',') ? str : str.replace(' ', 'T') + 'Z');
    return isNaN(d) ? 0 : Math.floor(d.getTime() / 1000);
}

// ── Crypto ────────────────────────────────────────────────────────────────

// SHA-256 fingerprint of a raw public key, formatted as colon-separated uppercase
// hex pairs (first 8 bytes only). e.g. "A3:F2:11:8C:44:D0:9E:7B"
// Used for out-of-band identity verification — matches the C++ client's format.
async function keyFingerprint(pkBytes) {
    const hash = await crypto.subtle.digest('SHA-256', pkBytes);
    return Array.from(new Uint8Array(hash).slice(0, 8))
        .map(b => b.toString(16).padStart(2, '0').toUpperCase())
        .join(':');
}

// TODO: fetch recipient's x25519_public_key and encrypt with HPKE.
// eslint-disable-next-line no-unused-vars
async function encryptForRecipient(_recipientUsername, _plaintext) {
    throw new Error('encryptForRecipient is not yet implemented — plaintext must not be sent');
}

// ── Unlock view (re-derive private key after page reload) ─────────────────
export function renderUnlock(container, navigate, onUnlocked) {
    const username = api.getUsername() ?? '';
    container.innerHTML = `
        <div class="auth-wrap">
            <div class="card">
                <h2>🔒 Session Locked</h2>
                <p>Your session is still active${username ? ` as <strong>${esc(username)}</strong>` : ''}. Re-enter your password to unlock.</p>
                <form id="unlock-form" novalidate>
                    <div class="form-group">
                        <label for="u-password">Password</label>
                        <input type="password" id="u-password" autocomplete="current-password" required autofocus>
                    </div>
                    <button type="submit" class="btn btn-primary">Unlock</button>
                    <div id="unlock-msg" role="alert"></div>
                </form>
                <p><a href="#" id="unlock-logout">Sign in as a different user</a></p>
            </div>
        </div>`;

    document.getElementById('unlock-logout').addEventListener('click', (e) => {
        e.preventDefault();
        api.logout();
        navigate('login');
    });

    const form = document.getElementById('unlock-form');
    const msg  = document.getElementById('unlock-msg');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        const password     = document.getElementById('u-password').value;
        const wrappedB64   = api.getStoredWrappedKey();
        const kekSalt      = api.getStoredKekSalt();

        if (!wrappedB64) {
            msg.className = 'error-msg';
            msg.textContent = 'Session expired — please sign in again.';
            setTimeout(() => { api.logout(); navigate('login'); }, 1500);
            return;
        }

        try {
            const parsed    = JSON.parse(atob(wrappedB64));
            const encrypted = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes = await decryptPrivateKey(encrypted, password);
            const privKey   = await crypto.subtle.importKey(
                'pkcs8', privBytes, { name: 'X25519' }, false, ['deriveKey', 'deriveBits'],
            );
            api.setPrivateKey(privKey);
            onUnlocked();
        } catch {
            msg.className = 'error-msg';
            msg.textContent = 'Incorrect password.';
            btn.disabled = false;
        }
    });
}

// ── Login view ────────────────────────────────────────────────────────────
export function renderLogin(container, navigate) {
    container.innerHTML = `
        <div class="auth-wrap">
            <div class="card">
                <h1>Sign in</h1>
                <form id="login-form" novalidate>
                    <div class="form-group">
                        <label for="l-username">Username</label>
                        <input type="text" id="l-username" autocomplete="username" required>
                    </div>
                    <div class="form-group">
                        <label for="l-password">Password</label>
                        <input type="password" id="l-password" autocomplete="current-password" required>
                    </div>
                    <button type="submit" class="btn btn-primary" style="width:100%">Sign in</button>
                    <div id="login-msg" role="alert"></div>
                </form>
                <div class="auth-toggle">
                    No account yet?
                    <button id="show-register">Register</button>
                </div>
            </div>
        </div>`;

    const form = document.getElementById('login-form');
    const msg  = document.getElementById('login-msg');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        try {
            await api.login(
                document.getElementById('l-username').value.trim(),
                document.getElementById('l-password').value
            );
            if (!api.getPrivateKey()) {
                msg.className = 'error-msg';
                msg.textContent = 'Your account is authenticated but your encryption key could not be loaded. Please contact support.';
                btn.disabled = false;
                return;
            }
            navigate('inbox');
        } catch (err) {
            msg.className = 'error-msg';
            msg.textContent = err.message;
            btn.disabled = false;
        }
    });

    document.getElementById('show-register').addEventListener('click', () => {
        renderRegister(container, navigate);
    });
}

// ── TOFU key pinning ──────────────────────────────────────────────────────
// On first contact with a user, their public key is pinned in localStorage.
// On subsequent contacts, if the key has changed the user is warned — this
// catches a compromised server swapping in a malicious key (MITM).
const TOFU_PREFIX = 'tofu_pk_';

function tofuCheck(username, b64PublicKey) {
    const stored = localStorage.getItem(TOFU_PREFIX + username);
    if (!stored) {
        // First time seeing this user — pin their key
        localStorage.setItem(TOFU_PREFIX + username, b64PublicKey);
        return { pinned: true, changed: false };
    }
    if (stored !== b64PublicKey) {
        // Key changed since we last saw this user — possible MITM
        return { pinned: false, changed: true, storedKey: stored };
    }
    return { pinned: false, changed: false };
}

// ── Password strength validation ──────────────────────────────────────────
function validatePassword(pw) {
    const errors = [];
    if (pw.length < 8)              errors.push('at least 8 characters');
    if (!/[A-Z]/.test(pw))          errors.push('one uppercase letter');
    if (!/[a-z]/.test(pw))          errors.push('one lowercase letter');
    if (!/[0-9]/.test(pw))          errors.push('one number');
    if (!/[^A-Za-z0-9]/.test(pw))   errors.push('one special character');
    return errors;
}

// ── Register view ─────────────────────────────────────────────────────────
function renderRegister(container, navigate) {
    container.innerHTML = `
        <div class="auth-wrap">
            <div class="card" style="position:center">
                <h1>Create account</h1>
                <form id="reg-form" novalidate>
                    <div class="form-group">
                        <label for="r-username">Username</label>
                        <input type="text" id="r-username" autocomplete="username" required
                               pattern="[A-Za-z0-9_]{3,32}" title="3–32 characters, letters, numbers and underscores only">
                        <div id="username-hint" class="pw-rules" style="justify-content:flex-start">
                            <span id="username-rule">✗ 3–32 chars, letters/numbers/underscores only</span>
                        </div>
                    </div>
                    <div class="form-group">
                        <label for="r-email">Email</label>
                        <input type="email" id="r-email" autocomplete="email" required>
                    </div>
                    <div class="form-group">
                        <label for="r-password">Password</label>
                        <input type="password" id="r-password" autocomplete="new-password" required>
                        <div id="pw-rules" class="pw-rules">
                            <span data-rule="length">✗ 8+ characters</span>
                            <span data-rule="upper">✗ Uppercase letter</span>
                            <span data-rule="lower">✗ Lowercase letter</span>
                            <span data-rule="number">✗ Number</span>
                            <span data-rule="special">✗ Special character</span>
                        </div>
                    </div>
                    <div class="form-group" id="r-confirm-group" hidden>
                        <label for="r-confirm">Confirm password</label>
                        <input type="password" id="r-confirm" autocomplete="new-password">
                    </div>
                    <button type="submit" class="btn btn-primary" style="width:100%">Register</button>
                    <div id="reg-msg" role="alert"></div>
                </form>
                <div class="auth-toggle">
                    Already have an account?
                    <button id="show-login">Sign in</button>
                </div>
            </div>
        </div>`;

    const form    = document.getElementById('reg-form');
    const msg     = document.getElementById('reg-msg');
    const pwInput = document.getElementById('r-password');
    const unInput = document.getElementById('r-username');

    // Live username rule indicator
    unInput.addEventListener('input', () => {
        const el = document.getElementById('username-rule');
        if (!el) return;
        const ok = /^[A-Za-z0-9_]{3,32}$/.test(unInput.value);
        el.textContent = (ok ? '✓ ' : '✗ ') + '3–32 chars, letters/numbers/underscores only';
        el.classList.toggle('pw-rule-ok', ok);
    });

    // Live password rule indicators
    const ruleChecks = {
        length:  pw => pw.length >= 8,
        upper:   pw => /[A-Z]/.test(pw),
        lower:   pw => /[a-z]/.test(pw),
        number:  pw => /[0-9]/.test(pw),
        special: pw => /[^A-Za-z0-9]/.test(pw),
    };

    pwInput.addEventListener('input', () => {
        const pw = pwInput.value;
        for (const [rule, check] of Object.entries(ruleChecks)) {
            const el = document.querySelector(`[data-rule="${rule}"]`);
            if (!el) continue;
            const ok = check(pw);
            el.textContent = (ok ? '✓ ' : '✗ ') + el.textContent.slice(2);
            el.classList.toggle('pw-rule-ok', ok);
        }
        // Show confirm field only once the user has started typing a password
        document.getElementById('r-confirm-group').hidden = pw.length === 0;
    });

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        try {
            const username = unInput.value.trim();
            const email    = document.getElementById('r-email').value.trim();
            const password = document.getElementById('r-password').value;
            const confirm  = document.getElementById('r-confirm').value;

            // Username format
            if (!/^[A-Za-z0-9_]{3,32}$/.test(username)) {
                msg.className = 'error-msg';
                msg.textContent = 'Username must be 3–32 characters: letters, numbers, and underscores only.';
                btn.disabled = false;
                return;
            }

            // Password strength
            const pwErrors = validatePassword(password);
            if (pwErrors.length > 0) {
                msg.className = 'error-msg';
                msg.textContent = `Password must contain: ${pwErrors.join(', ')}.`;
                btn.disabled = false;
                return;
            }

            // Confirm password
            if (password !== confirm) {
                msg.className = 'error-msg';
                msg.textContent = 'Passwords do not match.';
                btn.disabled = false;
                return;
            }

            const { publicKey, privateKey } = await generateKeypair();
            const pubKeyBytes = await getPublicKeyBytes(publicKey);
            const encryptedPrivateKey = await encryptPrivateKey(await getPrivateKeyBytes(privateKey), password);
            const toB64 = bytes => btoa(Array.from(bytes, b => String.fromCharCode(b)).join(''));
            await api.register(username, email, password, {
                x25519_public_key:  toB64(pubKeyBytes),
                wrapped_private_key: btoa(JSON.stringify(encryptedPrivateKey.toJSON())),
                kek_salt:           toB64(encryptedPrivateKey.salt),
            });
            msg.className = 'success-msg';
            msg.textContent = 'Account created — redirecting to sign in…';
            setTimeout(() => renderLogin(container, navigate), 1600);
        } catch (err) {
            msg.className = 'error-msg';
            msg.textContent = err.message;
            btn.disabled = false;
        }
    });

    document.getElementById('show-login').addEventListener('click', () => {
        renderLogin(container, navigate);
    });
}

// ── Inbox view ────────────────────────────────────────────────────────────
export async function renderInbox(container, navigate) {
    const myUsername = api.getUsername() ?? '';
    let currentConvMap = new Map();

    container.innerHTML = `
        <div class="chat-layout">
            <aside class="chat-sidebar">
                <div class="sidebar-header">
                    <span id="my-fingerprint" class="key-fingerprint" style="margin:0;font-size:.7rem" title="Your key fingerprint"></span>
                    <button class="btn btn-primary btn-sm" id="btn-compose">New Chat</button>
                </div>
                <div class="chat-list" id="conv-list">
                    <div class="loading"><span class="spinner"></span></div>
                </div>
            </aside>
            <section class="chat-main" id="inbox-body">
                <div class="chat-empty">Select a conversation or start a new chat</div>
            </section>
        </div>

        <!-- Change password modal (triggered from navbar) -->
        <dialog id="change-password-dialog">
            <form id="change-password-form" method="dialog" novalidate>
                <h3 style="margin-bottom:1.25rem">Change Password</h3>
                <div class="form-group">
                    <label for="cp-old">Current password</label>
                    <input type="password" id="cp-old" autocomplete="current-password" required>
                </div>
                <div class="form-group">
                    <label for="cp-new">New password</label>
                    <input type="password" id="cp-new" autocomplete="new-password" required>
                    <div id="cp-pw-rules" class="pw-rules">
                        <span data-rule="length">✗ 8+ characters</span>
                        <span data-rule="upper">✗ Uppercase letter</span>
                        <span data-rule="lower">✗ Lowercase letter</span>
                        <span data-rule="number">✗ Number</span>
                        <span data-rule="special">✗ Special character</span>
                    </div>
                </div>
                <div class="form-group">
                    <label for="cp-confirm">Confirm new password</label>
                    <input type="password" id="cp-confirm" autocomplete="new-password" required>
                </div>
                <div style="display:flex;gap:.75rem;margin-top:.25rem">
                    <button type="submit" class="btn btn-primary">Update Password</button>
                    <button type="button" class="btn btn-secondary" id="cp-cancel">Cancel</button>
                </div>
                <div id="cp-msg" role="alert"></div>
            </form>
        </dialog>`;

    // ── Fingerprint ───────────────────────────────────────────────────────
    const pubB64 = api.getPublicKeyB64();
    if (pubB64) {
        try {
            const pkBytes = Uint8Array.from(atob(pubB64), c => c.charCodeAt(0));
            const fp = await keyFingerprint(pkBytes);
            document.getElementById('my-fingerprint').textContent = `🔑 ${fp}`;
        } catch { /* non-fatal */ }
    }

    // ── New Chat button ───────────────────────────────────────────────────
    document.getElementById('btn-compose').addEventListener('click', () => {
        const partner = window.prompt('Start a chat with (enter username):')?.trim();
        if (!partner) return;
        renderThread(partner, currentConvMap.get(partner) ?? []);
    });

    // ── Change Password dialog (opened via navbar custom event) ───────────
    const cpDialog = document.getElementById('change-password-dialog');
    document.getElementById('cp-cancel').addEventListener('click', () => cpDialog.close());
    document.addEventListener('open-change-password', () => cpDialog.showModal(), { signal: AbortSignal.timeout(3_600_000) });

    // Live rule indicators for change-password dialog
    const cpNewInput = document.getElementById('cp-new');
    const cpRuleChecks = {
        length:  pw => pw.length >= 8,
        upper:   pw => /[A-Z]/.test(pw),
        lower:   pw => /[a-z]/.test(pw),
        number:  pw => /[0-9]/.test(pw),
        special: pw => /[^A-Za-z0-9]/.test(pw),
    };
    cpNewInput.addEventListener('input', () => {
        const pw = cpNewInput.value;
        for (const [rule, check] of Object.entries(cpRuleChecks)) {
            const el = document.querySelector(`#cp-pw-rules [data-rule="${rule}"]`);
            if (!el) continue;
            const ok = check(pw);
            el.textContent = (ok ? '✓ ' : '✗ ') + el.textContent.slice(2);
            el.classList.toggle('pw-rule-ok', ok);
        }
    });

    document.getElementById('change-password-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn     = e.target.querySelector('button[type="submit"]');
        const msgEl   = document.getElementById('cp-msg');
        const oldPw   = document.getElementById('cp-old').value;
        const newPw   = document.getElementById('cp-new').value;
        const confirm = document.getElementById('cp-confirm').value;

        msgEl.className = msgEl.textContent = '';

        if (newPw !== confirm) {
            msgEl.className = 'error-msg';
            msgEl.textContent = 'New passwords do not match.';
            return;
        }

        const pwErrors = validatePassword(newPw);
        if (pwErrors.length > 0) {
            msgEl.className = 'error-msg';
            msgEl.textContent = `Password must contain: ${pwErrors.join(', ')}.`;
            return;
        }

        btn.disabled = true;
        try {
            const wrappedB64 = api.getWrappedKey();
            if (!wrappedB64) throw new Error('No key material in session — please log in again.');

            const parsed    = JSON.parse(atob(wrappedB64));
            const encrypted = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes = await decryptPrivateKey(encrypted, oldPw);

            const newEncrypted  = await encryptPrivateKey(privBytes, newPw);
            const newWrappedB64 = btoa(JSON.stringify(newEncrypted.toJSON()));
            const newKekSalt    = btoa(String.fromCharCode(...newEncrypted.salt));

            await api.changePassword(oldPw, newPw, newWrappedB64, newKekSalt);
            api.setWrappedKey(newWrappedB64, newKekSalt);

            msgEl.className = 'success-msg';
            msgEl.textContent = 'Password updated successfully!';
            e.target.reset();
            setTimeout(() => { cpDialog.close(); msgEl.className = msgEl.textContent = ''; }, 1200);
        } catch (err) {
            msgEl.className = 'error-msg';
            msgEl.textContent = err.message;
        } finally {
            btn.disabled = false;
        }
    });

    const body    = document.getElementById('inbox-body');   // right panel
    const sidebar = document.getElementById('conv-list');    // left panel

    // ── Decrypt helper ────────────────────────────────────────────────────
    const senderKeyCache = {};

    async function tryDecrypt(msg) {
        // For sent messages, check the sessionStorage plaintext cache first.
        const msgId = msg.id ?? msg.message_id ?? '';
        if (msg.direction === 'sent') {
            const cached = sessionStorage.getItem(`sent_plain_${msgId}`);
            if (cached) return cached;
            return '(sent — encrypted)';
        }

        const privKey = api.getPrivateKey();
        if (!privKey || !msg.ciphertext || !msg.nonce || !msg.ephemeral_pk || !msg.sender_x25519_public_key) {
            return '(encrypted)';
        }
        try {
            const ciphertext = decodeField(msg.ciphertext);
            const nonce      = decodeField(msg.nonce);
            const ephPkBytes = decodeField(msg.ephemeral_pk);
            const ephPubKey  = await crypto.subtle.importKey(
                'raw', ephPkBytes, { name: 'X25519' }, false, ['deriveBits'],
            );

            const senderPkBytes = Uint8Array.from(atob(msg.sender_x25519_public_key), c => c.charCodeAt(0));
            if (!senderKeyCache[msg.sender_username]) {
                // TOFU: pin or verify sender's key
                const tofu = tofuCheck(msg.sender_username, msg.sender_x25519_public_key);
                if (tofu.changed) {
                    console.warn(`[TOFU] Key change detected for ${msg.sender_username} — possible MITM`);
                }
                senderKeyCache[msg.sender_username] = await crypto.subtle.importKey(
                    'raw', senderPkBytes, { name: 'X25519' }, false, ['deriveBits'],
                );
            }
            const senderStaticPubKey = senderKeyCache[msg.sender_username];

            const senderId    = msg.sender_id ?? '';
            const recipientId = msg.recipient_id ?? '';
            const timestamp   = parseTimestamp(msg.created_at ?? msg.timestamp);

            return await decryptMessage(
                ciphertext, nonce, ephPubKey, ephPkBytes,
                privKey, senderStaticPubKey,
                senderId, recipientId, msgId, timestamp,
            );
        } catch {
            return '(encrypted)';
        }
    }

    // ── Group messages into conversations ─────────────────────────────────
    // Returns Map<partnerUsername, msg[]> sorted by most-recent-first for list.
    function groupByConversation(msgs) {
        const convMap = new Map();
        for (const msg of msgs) {
            const partner = msg.direction === 'sent'
                ? (msg.recipient_username ?? 'Unknown')
                : (msg.sender_username ?? 'Unknown');
            if (!convMap.has(partner)) convMap.set(partner, []);
            convMap.get(partner).push(msg);
        }
        // Sort conversations by the timestamp of their latest message (newest first)
        return new Map(
            [...convMap.entries()].sort((a, b) => {
                const lastA = a[1].at(-1)?.created_at ?? '';
                const lastB = b[1].at(-1)?.created_at ?? '';
                return lastA < lastB ? 1 : -1;
            })
        );
    }

    // ── Render conversation list (left sidebar) ───────────────────────────
    function renderConvList(convMap, activePartner = null) {
        if (convMap.size === 0) {
            sidebar.innerHTML = `<div class="empty-state" style="padding:2rem 1rem;font-size:.875rem">No conversations yet</div>`;
            return;
        }

        const items = [...convMap.entries()].map(([partner, msgs]) => {
            const last    = msgs.at(-1);
            const preview = last?.content ?? '';
            const date    = formatDate(last?.created_at);
            const initial = (partner[0] ?? '?').toUpperCase();
            const active  = partner === activePartner ? ' active' : '';
            return `
                <div class="chat-item${active}" data-partner="${esc(partner)}" role="button" tabindex="0">
                    <div class="chat-avatar">${esc(initial)}</div>
                    <div class="chat-item-body">
                        <div class="chat-item-header">
                            <span class="chat-item-name">${esc(partner)}</span>
                            <span class="chat-item-date">${esc(date)}</span>
                        </div>
                        <div class="chat-item-preview">${esc(preview.slice(0, 60))}${preview.length > 60 ? '…' : ''}</div>
                    </div>
                </div>`;
        }).join('');

        sidebar.innerHTML = items;

        sidebar.querySelectorAll('.chat-item').forEach(item => {
            const open = () => renderThread(item.dataset.partner, convMap.get(item.dataset.partner) ?? []);
            item.addEventListener('click', open);
            item.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') open(); });
        });
    }

    // ── Send a message from within the thread ─────────────────────────────
    async function sendFromThread(partner, content, recipientUser) {
        const privKey  = api.getPrivateKey();
        const senderId = getUserId();
        if (!privKey || !senderId) throw new Error('Session key unavailable — please log in again.');

        // TOFU: warn if recipient's key has changed since we last saw it
        const tofu = tofuCheck(partner, recipientUser.x25519_public_key);
        if (tofu.changed) {
            const proceed = window.confirm(
                `⚠️ Warning: ${partner}'s encryption key has changed since your last conversation.\n\n` +
                `This could indicate a key rotation or a man-in-the-middle attack.\n\n` +
                `Verify their fingerprint out-of-band before continuing.\n\nSend anyway?`
            );
            if (!proceed) throw new Error('Send cancelled — please verify the recipient\'s key fingerprint.');
            // Update pin to the new key after user confirms
            localStorage.setItem(TOFU_PREFIX + partner, recipientUser.x25519_public_key);
        }

        const keyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
        const recipientPublicKey = await crypto.subtle.importKey('raw', keyBytes, { name: 'X25519' }, false, ['deriveBits']);

        const { ephPkBytes, nonce, ciphertext, messageId } = await encryptMessage(
            content, recipientPublicKey, privKey, senderId, recipientUser.id,
        );

        const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

        await sendMessage({
            recipient_id:             recipientUser.id,
            message_id:               messageId,
            ciphertext:               toHex(ciphertext),
            nonce:                    toHex(nonce),
            ephemeral_pk:             toHex(ephPkBytes),
            sender_x25519_public_key: api.getPublicKeyB64(),
        });

        sessionStorage.setItem(`sent_plain_${messageId}`, content);

        // Optimistically append the sent bubble without waiting for the next poll
        const sentMsg = {
            id: messageId, direction: 'sent',
            sender_username: myUsername, recipient_username: partner,
            content, created_at: new Date().toISOString(),
        };
        if (!currentConvMap.has(partner)) currentConvMap.set(partner, []);
        currentConvMap.get(partner).push(sentMsg);
        knownIds.add(messageId);

        const thread = document.getElementById('chat-thread');
        if (thread) {
            const tmp = document.createElement('div');
            tmp.innerHTML = buildBubble(sentMsg, myUsername);
            const bubble = tmp.firstElementChild;
            bubble.querySelectorAll('[data-action]').forEach(btn => {
                btn.addEventListener('click', () => handleAction(btn, body));
            });
            thread.appendChild(bubble);
            thread.scrollTop = thread.scrollHeight;
        }
    }

    // ── Render thread ─────────────────────────────────────────────────────
    function renderThread(partner, msgs) {
        const bubbles = msgs.map(msg => buildBubble(msg, myUsername)).join('');

        // Highlight active conversation in sidebar
        renderConvList(currentConvMap, partner);

        body.innerHTML = `
            <div class="thread-header">
                <div class="chat-avatar" style="width:36px;height:36px;font-size:.875rem;flex-shrink:0">${esc((partner[0] ?? '?').toUpperCase())}</div>
                <div class="thread-partner-info">
                    <span class="thread-partner">${esc(partner)}</span>
                    <span id="thread-fingerprint" class="key-fingerprint" style="margin:0"></span>
                </div>
            </div>
            <div class="chat-thread" id="chat-thread">
                ${bubbles || '<div class="empty-state" style="padding:2rem">No messages yet — say hello!</div>'}
            </div>
            <form class="send-bar" id="send-bar" novalidate>
                <input type="text" id="send-input" class="send-input" placeholder="Message…" autocomplete="off" required>
                <button type="submit" class="btn btn-primary send-btn">Send</button>
            </form>`;

        // Show partner's key fingerprint + TOFU warning if key changed
        getUser(partner).then(async user => {
            const fpEl = document.getElementById('thread-fingerprint');
            if (!fpEl || !user?.x25519_public_key) return;
            const pkBytes = Uint8Array.from(atob(user.x25519_public_key), c => c.charCodeAt(0));
            const fp = await keyFingerprint(pkBytes);
            const tofu = tofuCheck(partner, user.x25519_public_key);
            if (tofu.changed) {
                fpEl.textContent = `⚠️ Key changed! New: ${fp} — verify out-of-band`;
                fpEl.style.color = 'var(--danger)';
            } else {
                fpEl.textContent = `🔑 ${fp}`;
                fpEl.style.color = '';
            }
        }).catch(() => {});

        const thread = document.getElementById('chat-thread');
        thread.scrollTop = thread.scrollHeight;

        body.querySelectorAll('[data-action]').forEach(btn => {
            btn.addEventListener('click', () => handleAction(btn, body));
        });

        // ── Send bar ──────────────────────────────────────────────────────
        let cachedRecipient = null;

        document.getElementById('send-bar').addEventListener('submit', async (e) => {
            e.preventDefault();
            const input   = document.getElementById('send-input');
            const sendBtn = document.getElementById('send-bar').querySelector('button[type="submit"]');
            const content = input.value.trim();
            if (!content) return;

            sendBtn.disabled = true;
            input.disabled   = true;

            try {
                if (!cachedRecipient) cachedRecipient = await getUser(partner);
                if (!cachedRecipient?.x25519_public_key) throw new Error('Cannot find recipient key.');
                await sendFromThread(partner, content, cachedRecipient);
                input.value = '';
            } catch (err) {
                showInlineError(body, `Send failed: ${err.message}`);
            } finally {
                sendBtn.disabled = false;
                input.disabled   = false;
                input.focus();
            }
        });
    }

    // ── Initial load ──────────────────────────────────────────────────────
    let allMessages;
    try {
        const data = await api.getMessages();
        allMessages = data.messages ?? [];
    } catch (err) {
        body.innerHTML = `<div class="error-msg">Could not load messages: ${esc(err.message)}</div>`;
        return;
    }

    const decrypted = await Promise.all(
        allMessages.map(async msg => ({ ...msg, content: await tryDecrypt(msg) }))
    );

    currentConvMap = groupByConversation(decrypted);
    renderConvList(currentConvMap);

    // ── Poll for new messages ─────────────────────────────────────────────
    const knownIds = new Set(decrypted.map(m => String(m.id ?? m.message_id ?? '')));

    const pollInterval = setInterval(async () => {
        if (!body.isConnected) { clearInterval(pollInterval); return; }
        try {
            const data    = await api.getMessages();
            const fresh   = data.messages ?? [];
            const newMsgs = fresh.filter(m => !knownIds.has(String(m.id ?? m.message_id ?? '')));
            if (newMsgs.length === 0) return;

            const newDecrypted = await Promise.all(
                newMsgs.map(async msg => ({ ...msg, content: await tryDecrypt(msg) }))
            );

            for (const msg of newDecrypted) {
                knownIds.add(String(msg.id ?? msg.message_id ?? ''));
                const partner = msg.direction === 'sent'
                    ? (msg.recipient_username ?? 'Unknown')
                    : (msg.sender_username ?? 'Unknown');
                if (!currentConvMap.has(partner)) currentConvMap.set(partner, []);
                currentConvMap.get(partner).push(msg);
            }

            // Always re-render the sidebar to update previews
            const partnerEl    = body.querySelector('.thread-partner');
            const activePartner = partnerEl?.textContent?.trim() ?? '';
            renderConvList(currentConvMap, activePartner);

            // If a thread is open, append new bubbles for that thread
            const thread = body.querySelector('#chat-thread');
            if (thread) {
                const relevant = newDecrypted.filter(m => {
                    const p = m.direction === 'sent'
                        ? (m.recipient_username ?? 'Unknown')
                        : (m.sender_username ?? 'Unknown');
                    return p === activePartner;
                });
                for (const msg of relevant) {
                    const tmp = document.createElement('div');
                    tmp.innerHTML = buildBubble(msg, myUsername);
                    const bubble = tmp.firstElementChild;
                    bubble.querySelectorAll('[data-action]').forEach(btn => {
                        btn.addEventListener('click', () => handleAction(btn, body));
                    });
                    thread.appendChild(bubble);
                }
                if (relevant.length > 0) thread.scrollTop = thread.scrollHeight;
            }
        } catch { /* non-fatal */ }
    }, 10_000);
}

// ── Bubble renderers ──────────────────────────────────────────────────────
function buildBubble(msg, myUsername) {
    const id      = msg.id ?? msg.message_id ?? '';
    const isSent  = msg.direction === 'sent' || (msg.sender_username === myUsername);
    const content = msg.content ?? '(encrypted)';
    const date    = formatDate(msg.created_at ?? msg.timestamp);
    const sender  = msg.sender_username ?? 'Unknown';
    const sid     = esc(String(id));

    const avatarLabel = isSent
        ? esc((myUsername[0] ?? '?').toUpperCase())
        : esc((sender[0] ?? '?').toUpperCase());

    // Per spec:
    //   Forward  — both sender and recipient
    //   Download — both sender and recipient
    //   Delete   — sender only (hard deletes the row)
    //   Revoke   — sender only (blocks recipient access, row kept)
    const actions = `
        <button class="btn-icon" data-action="forward"  data-id="${sid}" title="Forward">↗️</button>
        <button class="btn-icon" data-action="download" data-id="${sid}" title="Download">⬇️</button>
        ${isSent ? `<button class="btn-icon" data-action="delete" data-id="${sid}" title="Delete">🗑️</button>` : ''}
        ${isSent ? `<button class="btn-revoke" data-action="revoke" data-id="${sid}" title="Revoke access">🚫 Revoke</button>` : ''}`;

    return `
        <div class="bubble-wrap ${isSent ? 'sent' : 'received'}">
            <div class="bubble-avatar">${avatarLabel}</div>
            <div class="bubble ${isSent ? 'sent' : 'received'} message-card" data-id="${esc(String(id))}" data-sender="${esc(sender)}">
                <div class="msg-body">${esc(String(content))}</div>
                <div class="bubble-footer">
                    ${date ? `<span class="msg-date">${date}</span>` : '<span></span>'}
                    <div class="msg-actions">${actions}</div>
                </div>
            </div>
        </div>`;
}

// Returns the entered username, or null if cancelled.
function showForwardDialog() {
    return new Promise(resolve => {
        // Re-use the existing dialog element if already in DOM, otherwise create one
        let dlg = document.getElementById('forward-dialog');
        if (!dlg) {
            dlg = document.createElement('dialog');
            dlg.id = 'forward-dialog';
            dlg.innerHTML = `
                <form id="forward-form" method="dialog" novalidate>
                    <h3 style="margin-bottom:1.25rem">Forward message</h3>
                    <div class="form-group">
                        <label for="fwd-recipient">Forward to username</label>
                        <input type="text" id="fwd-recipient" autocomplete="off" placeholder="Enter username" required>
                        <div id="fwd-fingerprint" class="key-fingerprint"></div>
                    </div>
                    <div style="display:flex;gap:.75rem;margin-top:.25rem">
                        <button type="submit" class="btn btn-primary">Forward</button>
                        <button type="button" class="btn btn-secondary" id="fwd-cancel">Cancel</button>
                    </div>
                    <div id="fwd-msg" role="alert"></div>
                </form>`;
            document.body.appendChild(dlg);
        }

        const form      = dlg.querySelector('#forward-form');
        const input     = dlg.querySelector('#fwd-recipient');
        const fpEl      = dlg.querySelector('#fwd-fingerprint');
        const msgEl     = dlg.querySelector('#fwd-msg');
        const cancelBtn = dlg.querySelector('#fwd-cancel');

        // Reset state
        input.value = '';
        fpEl.textContent = '';
        msgEl.className = msgEl.textContent = '';

        // Show fingerprint on blur
        input.addEventListener('blur', async () => {
            const username = input.value.trim();
            if (!username) { fpEl.textContent = ''; return; }
            try {
                const user = await getUser(username);
                if (!user?.x25519_public_key) { fpEl.textContent = '(user not found)'; return; }
                const pkBytes = Uint8Array.from(atob(user.x25519_public_key), c => c.charCodeAt(0));
                const fp = await keyFingerprint(pkBytes);
                fpEl.textContent = `🔑 ${fp}`;
            } catch { fpEl.textContent = ''; }
        }, { once: false });

        cancelBtn.onclick = () => { dlg.close(); resolve(null); };

        form.onsubmit = (e) => {
            e.preventDefault();
            const username = input.value.trim();
            if (!username) {
                msgEl.className = 'error-msg';
                msgEl.textContent = 'Please enter a username.';
                return;
            }
            dlg.close();
            resolve(username);
        };

        dlg.showModal();
        input.focus();
    });
}

async function handleAction(btn, inboxBody) {
    const { action, id } = btn.dataset;
    btn.disabled = true;

    try {
        switch (action) {
            case 'delete':
                await api.deleteMessage(id);
                (btn.closest('.bubble-wrap') ?? btn.closest('.message-card'))?.remove();
                break;

            case 'revoke':
                await api.revokeMessage(id);
                (btn.closest('.bubble-wrap') ?? btn.closest('.message-card'))?.remove();
                break;

            case 'download': {
                const card    = btn.closest('.message-card');
                const sender  = card.dataset.sender ?? card.querySelector('.msg-sender')?.textContent ?? 'Unknown';
                const date    = card.querySelector('.msg-date')?.textContent ?? '';
                const content = card.querySelector('.msg-body').textContent;

                const text = `From: ${sender}\nDate: ${date}\n\n${content}`;
                const blob = new Blob([text], { type: 'text/plain' });
                const url  = URL.createObjectURL(blob);

                const a    = document.createElement('a');
                a.href     = url;
                a.download = `message-${id}.txt`;
                a.click();
                URL.revokeObjectURL(url);

                btn.disabled = false;
                return;
            }

            case 'forward': {
                // Show a dialog to pick the recipient instead of window.prompt
                const recipientUsername = await showForwardDialog();
                if (!recipientUsername) { btn.disabled = false; return; }

                const privKey  = api.getPrivateKey();
                const myUserId = getUserId();
                if (!privKey || !myUserId) throw new Error('Session key unavailable — please log in again.');

                const [recipientUser, orig] = await Promise.all([
                    api.getUser(recipientUsername),
                    api.getMessage(id),
                ]);

                if (!recipientUser?.x25519_public_key) throw new Error('Recipient not found or has no encryption key.');
                if (!orig?.ciphertext || !orig?.nonce || !orig?.ephemeral_pk) throw new Error('Original message has no crypto fields.');
                if (!orig?.sender_x25519_public_key) throw new Error('Original sender key unavailable.');

                const origEphPkBytes = decodeField(orig.ephemeral_pk);
                const origEphPubKey  = await crypto.subtle.importKey(
                    'raw', origEphPkBytes, { name: 'X25519' }, false, ['deriveBits'],
                );
                const origSenderKeyBytes = Uint8Array.from(atob(orig.sender_x25519_public_key), c => c.charCodeAt(0));
                const origSenderPubKey   = await crypto.subtle.importKey('raw', origSenderKeyBytes, { name: 'X25519' }, false, ['deriveBits']);

                const origCt    = decodeField(orig.ciphertext);
                const origNonce = decodeField(orig.nonce);
                const origTs    = parseTimestamp(orig.created_at);
                const plaintext = await decryptMessage(
                    origCt, origNonce, origEphPubKey, origEphPkBytes,
                    privKey, origSenderPubKey,
                    orig.sender_id ?? '', orig.recipient_id ?? '', orig.id, origTs,
                );

                const recipKeyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
                const recipPublicKey = await crypto.subtle.importKey('raw', recipKeyBytes, { name: 'X25519' }, false, ['deriveBits']);
                const { ephPkBytes: fwdEphPkBytes, nonce, ciphertext, messageId } = await encryptMessage(
                    plaintext, recipPublicKey, privKey, myUserId, recipientUser.id,
                );

                const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

                await api.forwardMessage(id, {
                    recipientUsername,
                    message_id:   messageId,
                    ciphertext:   toHex(ciphertext),
                    nonce:        toHex(nonce),
                    ephemeral_pk: toHex(fwdEphPkBytes),
                });

                // Cache plaintext so the forwarded copy shows in the sender's sent bubble
                sessionStorage.setItem(`sent_plain_${messageId}`, plaintext);

                // Visual feedback on the icon button
                const origLabel = btn.textContent;
                btn.textContent = '✅';
                setTimeout(() => { btn.disabled = false; btn.textContent = origLabel; }, 1500);
                return;
            }
        }

        // If no messages remain, show the empty state
        if (!inboxBody.querySelector('.message-card')) {
            inboxBody.innerHTML = `<div class="empty-state">Your inbox is empty.</div>`;
        }
    } catch (err) {
        showInlineError(inboxBody, `Action failed: ${err.message}`);
        btn.disabled = false;
    }
}

// ── Utility ───────────────────────────────────────────────────────────────
function showInlineError(container, text) {
    const el = document.createElement('div');
    el.className = 'error-msg';
    el.textContent = text;
    container.prepend(el);
    setTimeout(() => el.remove(), 4000);
}
