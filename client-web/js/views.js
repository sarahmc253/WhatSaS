/**
 * views.js — render functions for Login, Inbox, and Compose views.
 *
 * Each function writes HTML into `container` (the #app element) then
 * wires up event listeners.  They never touch the DOM outside their
 * container, keeping routing concerns in app.js.
 */

import * as api from './api.js';
import { getUser, sendMessage } from './api.js';
import encryptMessage, { decryptMessage } from '../crypto/messageEncryption.js';

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

// ── Crypto ────────────────────────────────────────────────────────────────

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
            // TODO: generate X25519 key pair, derive KEK via Argon2id, HPKE-wrap
            //       the private key, then call api.register() with real key material.
            throw new Error('Registration is not yet implemented — key generation and HPKE wrapping are pending');
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
            <button class="btn btn-primary" id="btn-compose">Compose</button>
        </div>
        <div id="inbox-body">
            <div class="loading"><span class="spinner"></span> Loading…</div>
        </div>`;

    document.getElementById('btn-compose').addEventListener('click', () => navigate('compose'));

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

    async function tryDecrypt(msg) {
        const privKey = api.getPrivateKey();
        if (!privKey || !msg.ciphertext || !msg.nonce || !msg.ephemeral_pk) {
            return '(encrypted)';
        }
        try {
            const ciphertext  = hexToBytes(msg.ciphertext);
            const nonce       = hexToBytes(msg.nonce);
            const ephPubKey   = await crypto.subtle.importKey(
                'raw', hexToBytes(msg.ephemeral_pk), { name: 'X25519' }, false, ['deriveKey', 'deriveBits'],
            );
            return await decryptMessage(ciphertext, nonce, ephPubKey, privKey);
        } catch {
            return '(encrypted)';
        }
    }

    const decrypted = await Promise.all(
        messages.map(async msg => ({ ...msg, content: await tryDecrypt(msg) }))
    );

    body.innerHTML = `
        <div class="message-list">
            ${decrypted.map(buildMessageCard).join('')}
        </div>`;

    body.querySelectorAll('[data-action]').forEach((btn) => {
        btn.addEventListener('click', () => handleAction(btn, body));
    });
}

function buildMessageCard(msg) {
    // Gracefully handle whatever field names the server settles on
    const id     = msg.id ?? msg.message_id ?? '';
    const sender = msg.sender_username ?? msg.sender ?? msg.sender_id ?? 'Unknown';
    const body   = msg.content ?? msg.body ?? '(encrypted)';
    const date   = formatDate(msg.timestamp ?? msg.created_at);

    return `
        <div class="message-card" data-id="${esc(String(id))}">
            <div class="msg-header">
                <span class="msg-sender">${esc(String(sender))}</span>
                ${date ? `<span class="msg-date">${date}</span>` : ''}
            </div>
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

            case 'forward':
                await api.forwardMessage(id);
                // Brief visual confirmation instead of a disruptive alert
                btn.textContent = 'Forwarded!';
                setTimeout(() => { btn.disabled = false; btn.textContent = 'Forward'; }, 1500);
                return; // skip the re-enable at the bottom
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
            const recipientUser = await getUser(recipient);
            if (!recipientUser?.x25519_public_key) {
                throw new Error('Recipient not found or has no encryption key.');
            }

            const keyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
            const recipientPublicKey = await crypto.subtle.importKey('raw', keyBytes, { name: 'X25519' }, false, []);

            const { ephemeralPublicKey, nonce, ciphertext } = await encryptMessage(content, recipientPublicKey);

            const toHex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
            const ephemeralPkBytes = new Uint8Array(await crypto.subtle.exportKey('raw', ephemeralPublicKey));

            await sendMessage({
                recipient_id: recipientUser.id,
                ciphertext:   toHex(ciphertext),
                nonce:        toHex(nonce),
                ephemeral_pk: toHex(ephemeralPkBytes),
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
