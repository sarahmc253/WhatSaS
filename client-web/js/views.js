/**
 * views.js — render functions for Login, Inbox, and Compose views.
 *
 * Each function writes HTML into `container` (the #app element) then
 * wires up event listeners.  They never touch the DOM outside their
 * container, keeping routing concerns in app.js.
 */

import * as api from './api.js';
import { getUser, sendMessage, getUserId, flushMessages } from './api.js';
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

// ── Register view ─────────────────────────────────────────────────────────
function renderRegister(container, navigate) {
    container.innerHTML = `
        <div class="auth-wrap">
            <div class="card">
                <h1>Create account</h1>
                <form id="reg-form" novalidate>
                    <div class="form-group">
                        <label for="r-username">Username</label>
                        <input type="text" id="r-username" autocomplete="username" required>
                    </div>
                    <div class="form-group">
                        <label for="r-email">Email</label>
                        <input type="email" id="r-email" autocomplete="email" required>
                    </div>
                    <div class="form-group">
                        <label for="r-password">Password</label>
                        <input type="password" id="r-password" autocomplete="new-password" required>
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

    const form = document.getElementById('reg-form');
    const msg  = document.getElementById('reg-msg');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        try {
            const username = document.getElementById('r-username').value.trim();
            const email    = document.getElementById('r-email').value.trim();
            const password = document.getElementById('r-password').value;

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
    // Render header + loading state immediately so the UI isn't blank
    container.innerHTML = `
        <div class="inbox-header">
            <h2>Inbox</h2>
            <div class="inbox-actions">
                <button class="btn btn-primary" id="btn-compose">Compose</button>
                <button class="btn btn-secondary" id="btn-anchor-now">Anchor now</button>
                <button class="btn btn-secondary" id="btn-change-password">🔒 Change Password</button>
            </div>
        </div>
        <div id="my-fingerprint" class="key-fingerprint" title="Your key fingerprint — share this with contacts to verify your identity"></div>
        <section id="change-password-section" class="card" hidden>
            <h3>Change Password</h3>
            <form id="change-password-form" novalidate>
                <div class="form-group">
                    <label for="cp-old">Current password</label>
                    <input type="password" id="cp-old" autocomplete="current-password" required>
                </div>
                <div class="form-group">
                    <label for="cp-new">New password</label>
                    <input type="password" id="cp-new" autocomplete="new-password" required>
                </div>
                <div class="form-group">
                    <label for="cp-confirm">Confirm new password</label>
                    <input type="password" id="cp-confirm" autocomplete="new-password" required>
                </div>
                <button type="submit" class="btn btn-primary">Update Password</button>
                <div id="cp-msg" role="alert"></div>
            </form>
        </section>
        <div id="inbox-body">
            <div class="loading"><span class="spinner"></span> Loading…</div>
        </div>`;

    // Compute and display own fingerprint from the login-cached public key
    const pubB64 = api.getPublicKeyB64();
    if (pubB64) {
        try {
            const pkBytes = Uint8Array.from(atob(pubB64), c => c.charCodeAt(0));
            const fp = await keyFingerprint(pkBytes);
            document.getElementById('my-fingerprint').textContent = `🔑 Your fingerprint: ${fp}`;
        } catch { /* non-fatal */ }
    }

    document.getElementById('btn-compose').addEventListener('click', () => navigate('compose'));

    document.getElementById('btn-anchor-now').addEventListener('click', async (e) => {
        const btn = e.currentTarget;
        btn.disabled = true;
        try {
            await flushMessages();
            btn.textContent = 'Anchoring…';
            await new Promise(resolve => setTimeout(resolve, 5000));
            renderInbox(container, navigate);
        } catch {
            btn.disabled = false;
            btn.textContent = 'Anchor failed';
            setTimeout(() => { btn.textContent = 'Anchor now'; }, 2000);
        }
    });

    document.getElementById('btn-change-password').addEventListener('click', () => {
        const section = document.getElementById('change-password-section');
        section.hidden = !section.hidden;
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

        btn.disabled = true;
        try {
            const wrappedB64 = api.getWrappedKey();
            if (!wrappedB64) throw new Error('No key material in session — please log in again.');

            // Decrypt private key with old password
            const parsed    = JSON.parse(atob(wrappedB64));
            const encrypted = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes = await decryptPrivateKey(encrypted, oldPw);

            // Re-encrypt under new password
            const newEncrypted = await encryptPrivateKey(privBytes, newPw);
            const newWrappedB64 = btoa(JSON.stringify(newEncrypted.toJSON()));
            // kek_salt: use the new salt embedded in the encrypted struct (base64)
            const newKekSalt = btoa(String.fromCharCode(...newEncrypted.salt));

            await api.changePassword(oldPw, newPw, newWrappedB64, newKekSalt);

            // Keep the in-memory wrapped key state consistent with the new password
            api.setWrappedKey(newWrappedB64, newKekSalt);

            msgEl.className = 'success-msg';
            msgEl.textContent = 'Password updated successfully!';
            e.target.reset();
            document.getElementById('change-password-section').hidden = true;
        } catch (err) {
            msgEl.className = 'error-msg';
            msgEl.textContent = err.message;
        } finally {
            btn.disabled = false;
        }
    });

    const body = document.getElementById('inbox-body');
    let messages;

    try {
        const data = await api.getMessages();
messages = data.messages ?? [];
    } catch (err) {
        body.innerHTML = `<div class="error-msg">Could not load messages: ${esc(err.message)}</div>`;
        return;
    }

    if (messages.length === 0) {
        body.innerHTML = `<div class="empty-state">Your inbox is empty.</div>`;
        return;
    }

    // Cache sender public keys to avoid repeated fetches for the same sender within one render.
    const senderKeyCache = {};

    async function tryDecrypt(msg) {
        const privKey = api.getPrivateKey();
        if (!privKey || !msg.ciphertext || !msg.nonce || !msg.ephemeral_pk || !msg.sender_x25519_public_key) {
            return '(encrypted)';
        }
        try {
            const ciphertext = decodeField(msg.ciphertext);
            const nonce      = decodeField(msg.nonce);
            const ephPkBytes = decodeField(msg.ephemeral_pk);
            const ephPubKey  = await crypto.subtle.importKey(
                'raw', ephPkBytes, { name: 'X25519' }, false, [],
            );

            const senderPkBytes = Uint8Array.from(atob(msg.sender_x25519_public_key), c => c.charCodeAt(0));
            if (!senderKeyCache[msg.sender_username]) {
                senderKeyCache[msg.sender_username] = await crypto.subtle.importKey(
                    'raw', senderPkBytes, { name: 'X25519' }, false, [],
                );
            }
            const senderStaticPubKey = senderKeyCache[msg.sender_username];

            const senderId    = msg.sender_id ?? '';
            const recipientId = msg.recipient_id ?? '';
            const messageId   = msg.id ?? msg.message_id ?? '';
            const timestamp   = parseTimestamp(msg.created_at ?? msg.timestamp);

            return await decryptMessage(
                ciphertext, nonce, ephPubKey, ephPkBytes,
                privKey, senderStaticPubKey,
                senderId, recipientId, messageId, timestamp,
            );
        } catch {
            return '(encrypted)';
        }
    }

    const decrypted = await Promise.all(
        messages.map(async msg => {
            const content = await tryDecrypt(msg);
            if (msg.ciphertext && msg.id) {
                try {
                    const hashBuf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(msg.ciphertext));
                    const hex = '0x' + Array.from(new Uint8Array(hashBuf), b => b.toString(16).padStart(2, '0')).join('');
                    localStorage.setItem(`hash:${msg.id}`, JSON.stringify({
                        contentHash: hex,
                        txHash:      msg.tx_hash ?? null,
                        createdAt:   msg.created_at ?? null,
                    }));
                } catch { /* non-fatal */ }
            }
            return { ...msg, content };
        })
    );

    body.innerHTML = `
        <div class="message-list">
            ${decrypted.map(buildMessageCard).join('')}
        </div>`;

    body.querySelectorAll('[data-action]').forEach((btn) => {
        btn.addEventListener('click', () => handleAction(btn, body));
    });

    body.querySelectorAll('code.merkle-root').forEach((el) => {
        el.addEventListener('click', () => navigator.clipboard.writeText(el.textContent));
    });
}

function buildMessageCard(msg) {
    // Gracefully handle whatever field names the server settles on
    const id     = msg.id ?? msg.message_id ?? '';
    const sender = msg.sender_username ?? msg.sender ?? msg.sender_id ?? 'Unknown';
    const body   = msg.content ?? msg.body ?? '(encrypted)';
    const date   = formatDate(msg.timestamp ?? msg.created_at);

    const anchorBadge = msg.tx_hash
        ? `<span class="badge badge-anchored">&#9875; ${esc(String(msg.tx_hash).slice(0, 10))}</span>`
        : `<span class="badge badge-unanchored">Not anchored</span>`;
    const merkleEl = msg.merkle_root
        ? `<code class="merkle-root" title="Click to copy">${esc(String(msg.merkle_root))}</code>`
        : '';

    return `
        <div class="message-card" data-id="${esc(String(id))}">
            <div class="msg-header">
                <span class="msg-sender">${esc(String(sender))}</span>
                ${date ? `<span class="msg-date">${date}</span>` : ''}
                ${anchorBadge}
            </div>
            ${merkleEl}
            <div class="msg-body">${esc(String(body))}</div>
            <div class="msg-actions">
                <button class="btn-action" data-action="forward" data-id="${esc(String(id))}">Forward</button>
                <button class="btn-action danger" data-action="revoke"  data-id="${esc(String(id))}">Revoke</button>
                <button class="btn-action danger" data-action="delete"  data-id="${esc(String(id))}">Delete</button>
            </div>
        </div>`;
}

async function handleAction(btn, inboxBody) {
    const { action, id } = btn.dataset;
    btn.disabled = true;

    try {
        switch (action) {
            case 'delete':
                await api.deleteMessage(id);
                btn.closest('.message-card')?.remove();
                break;

            case 'revoke':
                await api.revokeMessage(id);
                btn.closest('.message-card')?.remove();
                break;

            case 'forward': {
                const recipientUsername = window.prompt('Forward to username:')?.trim();
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
                const origSenderPubKey   = await crypto.subtle.importKey('raw', origSenderKeyBytes, { name: 'X25519' }, false, []);

                const origCt    = decodeField(orig.ciphertext);
                const origNonce = decodeField(orig.nonce);
                // orig from GET /messages/:id includes sender_id, recipient_id, created_at for AD
                const origTs = parseTimestamp(orig.created_at);
                const plaintext = await decryptMessage(
                    origCt, origNonce, origEphPubKey, origEphPkBytes,
                    privKey, origSenderPubKey,
                    orig.sender_id ?? '', orig.recipient_id ?? '', orig.id, origTs,
                );

                const recipKeyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
                const recipPublicKey = await crypto.subtle.importKey('raw', recipKeyBytes, { name: 'X25519' }, false, []);
                const { ephPkBytes: fwdEphPkBytes, nonce, ciphertext, messageId, timestamp } = await encryptMessage(
                    plaintext, recipPublicKey, privKey, myUserId, recipientUser.id,
                );

                const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');

                await api.forwardMessage(id, {
                    recipientUsername,
                    message_id:   messageId,
                    ciphertext:   toHex(ciphertext),
                    nonce:        toHex(nonce),
                    ephemeral_pk: toHex(fwdEphPkBytes),
                    timestamp,
                });

                btn.textContent = 'Forwarded!';
                setTimeout(() => { btn.disabled = false; btn.textContent = 'Forward'; }, 1500);
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

// ── Compose view ──────────────────────────────────────────────────────────
export function renderCompose(container, navigate) {
    container.innerHTML = `
        <div class="compose-header">
            <button class="btn btn-secondary" id="btn-back">← Back</button>
            <h2>New Message</h2>
        </div>
        <div class="card">
            <form id="compose-form" novalidate>
                <div class="form-group">
                    <label for="c-recipient">Recipient username</label>
                    <input type="text" id="c-recipient" placeholder="Enter username" required>
                    <div id="recipient-fingerprint" class="key-fingerprint"></div>
                </div>
                <div class="form-group">
                    <label for="c-body">Message</label>
                    <textarea id="c-body" placeholder="Write your message…" required></textarea>
                </div>
                <div class="compose-actions">
                    <button type="submit" class="btn btn-primary">Send</button>
                    <button type="button" class="btn btn-secondary" id="btn-cancel">Cancel</button>
                </div>
                <div id="compose-msg" role="alert"></div>
            </form>
        </div>`;

    document.getElementById('btn-back').addEventListener('click',   () => navigate('inbox'));
    document.getElementById('btn-cancel').addEventListener('click', () => navigate('inbox'));

    // Show peer fingerprint when the user finishes typing a recipient username
    document.getElementById('c-recipient').addEventListener('blur', async (e) => {
        const username = e.target.value.trim();
        const fpEl = document.getElementById('recipient-fingerprint');
        if (!username) { fpEl.textContent = ''; return; }
        try {
            const user = await getUser(username);
            if (!user?.x25519_public_key) { fpEl.textContent = ''; return; }
            const pkBytes = Uint8Array.from(atob(user.x25519_public_key), c => c.charCodeAt(0));
            const fp = await keyFingerprint(pkBytes);
            fpEl.textContent = `🔑 ${esc(username)}'s fingerprint: ${fp}`;
        } catch { fpEl.textContent = ''; }
    });

    const form = document.getElementById('compose-form');
    const msg  = document.getElementById('compose-msg');

    form.addEventListener('submit', async (e) => {
        e.preventDefault();

        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        const recipient = document.getElementById('c-recipient').value.trim();
        const content   = document.getElementById('c-body').value.trim();

        if (!recipient || !content) {
            msg.className = 'error-msg';
            msg.textContent = 'Recipient and message are required.';
            btn.disabled = false;
            return;
        }

        try {
            const senderPrivateKey = api.getPrivateKey();
            if (!senderPrivateKey) throw new Error('Private key unavailable — please log in again.');

            const recipientUser = await getUser(recipient);
            if (!recipientUser?.x25519_public_key) {
                throw new Error('Recipient not found or has no encryption key.');
            }

            const senderPrivKey = api.getPrivateKey();
            const senderId      = getUserId();
            if (!senderPrivKey || !senderId) throw new Error('Session key unavailable — please log in again.');

            const keyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
            console.log('[compose] recipient x25519 key byte length:', keyBytes.byteLength);
            const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
            console.log('[compose] recipient x25519 key first/last 4 bytes:', toHex(keyBytes.slice(0, 4)), '...', toHex(keyBytes.slice(-4)));
            const recipientPublicKey = await crypto.subtle.importKey('raw', keyBytes, { name: 'X25519' }, false, []);

            const { ephPkBytes, nonce, ciphertext, messageId, timestamp } = await encryptMessage(
                content, recipientPublicKey, senderPrivKey, senderId, recipientUser.id,
            );

            await sendMessage({
                recipient_id: recipientUser.id,
                message_id:   messageId,
                ciphertext:   toHex(ciphertext),
                nonce:        toHex(nonce),
                ephemeral_pk: toHex(ephPkBytes),
                timestamp,
            });
            msg.className = 'success-msg';
            msg.textContent = 'Message sent!';
            form.reset();
        } catch (err) {
            msg.className = 'error-msg';
            msg.textContent = err.message;
        } finally {
            btn.disabled = false;
        }
    });
}

// ── Utility ───────────────────────────────────────────────────────────────
function showInlineError(container, text) {
    const el = document.createElement('div');
    el.className = 'error-msg';
    el.textContent = text;
    container.prepend(el);
    setTimeout(() => el.remove(), 4000);
}
