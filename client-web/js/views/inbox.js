import * as api from '../api.js';
import { getUser, sendMessage, getUserId } from '../api.js';
import { encryptMessage, decryptMessage } from '../../crypto/messageEncryption.js';
import { encryptPrivateKey, decryptPrivateKey, EncryptedPrivateKey } from '../../crypto/keyStorage.js';
import {
    esc, formatDate, decodeField, parseTimestamp,
    keyFingerprint, tofuCheck, TOFU_PREFIX,
    validatePassword, showInlineError,
} from './helpers.js';

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
    document.getElementById('btn-compose').addEventListener('click', async () => {
        const partner = await showNewChatDialog();
        if (!partner) return;
        renderThread(partner, currentConvMap.get(partner) ?? []);
    });

    // ── Change Password dialog ────────────────────────────────────────────
    const cpDialog   = document.getElementById('change-password-dialog');
    const inboxAbort = new AbortController();
    document.getElementById('cp-cancel').addEventListener('click', () => cpDialog.close());
    document.addEventListener('open-change-password', () => cpDialog.showModal(), { signal: inboxAbort.signal });

    const cpNewInput    = document.getElementById('cp-new');
    const cpRuleChecks  = {
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

            const parsed       = JSON.parse(atob(wrappedB64));
            const encrypted    = EncryptedPrivateKey.fromJSON(parsed);
            const privBytes    = await decryptPrivateKey(encrypted, oldPw);
            const newEncrypted = await encryptPrivateKey(privBytes, newPw);
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

    const body    = document.getElementById('inbox-body');
    const sidebar = document.getElementById('conv-list');

    // ── Decrypt helper ────────────────────────────────────────────────────
    const senderKeyCache = {};

    async function tryDecrypt(msg) {
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
                'raw', ephPkBytes, { name: 'X25519' }, false, [],
            );

            const senderPkBytes = Uint8Array.from(atob(msg.sender_x25519_public_key), c => c.charCodeAt(0));
            if (!senderKeyCache[msg.sender_username]) {
                const tofu = tofuCheck(msg.sender_username, msg.sender_x25519_public_key);
                if (tofu.changed) {
                    console.warn(`[TOFU] Key change detected for ${msg.sender_username} — possible MITM`);
                }
                senderKeyCache[msg.sender_username] = await crypto.subtle.importKey(
                    'raw', senderPkBytes, { name: 'X25519' }, false, [],
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
    function groupByConversation(msgs) {
        const convMap = new Map();
        for (const msg of msgs) {
            const partner = msg.direction === 'sent'
                ? (msg.recipient_username ?? 'Unknown')
                : (msg.sender_username ?? 'Unknown');
            if (!convMap.has(partner)) convMap.set(partner, []);
            convMap.get(partner).push(msg);
        }
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
            item.addEventListener('keydown', e => {
                if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    open();
                }
            });
        });
    }

    // ── Send a message from within the thread ─────────────────────────────
    async function sendFromThread(partner, content, recipientUser) {
        const privKey  = api.getPrivateKey();
        const senderId = getUserId();
        if (!privKey || !senderId) throw new Error('Session key unavailable — please log in again.');

        const tofu = tofuCheck(partner, recipientUser.x25519_public_key);
        if (tofu.changed) {
            const proceed = await showKeyChangedDialog(partner);
            if (!proceed) throw new Error('Send cancelled — please verify the recipient\'s key fingerprint.');
            localStorage.setItem(TOFU_PREFIX + partner, recipientUser.x25519_public_key);
        }

        const keyBytes = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
        const recipientPublicKey = await crypto.subtle.importKey('raw', keyBytes, { name: 'X25519' }, false, []);

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
                btn.addEventListener('click', () => handleAction(btn, body, currentConvMap, myUsername));
            });
            bubble.querySelectorAll('[data-copy]').forEach(btn => {
                btn.addEventListener('click', () => navigator.clipboard.writeText(btn.dataset.copy));
            });
            thread.appendChild(bubble);
            thread.scrollTop = thread.scrollHeight;
        }
    }

    // ── Render thread ─────────────────────────────────────────────────────
    function renderThread(partner, msgs) {
        const bubbles = msgs.map(msg => buildBubble(msg, myUsername)).join('');
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
            btn.addEventListener('click', () => handleAction(btn, body, currentConvMap, myUsername));
        });
        body.querySelectorAll('[data-copy]').forEach(btn => {
            btn.addEventListener('click', () => navigator.clipboard.writeText(btn.dataset.copy));
        });

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
        allMessages.map(async msg => {
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

    currentConvMap = groupByConversation(decrypted);
    renderConvList(currentConvMap);

    // ── Poll for new messages ─────────────────────────────────────────────
    const knownIds = new Set(decrypted.map(m => String(m.id ?? m.message_id ?? '')));

    const pollInterval = setInterval(async () => {
        if (!body.isConnected) { clearInterval(pollInterval); inboxAbort.abort(); return; }
        try {
            const data    = await api.getMessages();
            const fresh   = data.messages ?? [];
            const newMsgs     = fresh.filter(m => !knownIds.has(String(m.id ?? m.message_id ?? '')));
            const changedMsgs = fresh.filter(m => {
                const id  = String(m.id ?? m.message_id ?? '');
                if (!knownIds.has(id)) return false;
                // detect server-side state changes (revoke, edit)
                for (const msgs of currentConvMap.values()) {
                    const existing = msgs.find(e => String(e.id ?? e.message_id ?? '') === id);
                    if (existing && (existing.is_revoked !== m.is_revoked || existing.content_hash !== m.content_hash)) return true;
                }
                return false;
            });

            if (newMsgs.length === 0 && changedMsgs.length === 0) return;

            const newDecrypted = await Promise.all(
                newMsgs.map(async msg => ({ ...msg, content: await tryDecrypt(msg) }))
            );

            // update changed entries in-place so sidebar previews stay fresh
            for (const msg of changedMsgs) {
                const id = String(msg.id ?? msg.message_id ?? '');
                for (const msgs of currentConvMap.values()) {
                    const idx = msgs.findIndex(e => String(e.id ?? e.message_id ?? '') === id);
                    if (idx !== -1) {
                        msgs[idx] = { ...msgs[idx], ...msg, content: msg.is_revoked ? msgs[idx].content : await tryDecrypt(msg) };
                        // patch live DOM bubble if visible
                        const liveBubble = document.querySelector(`.message-card[data-id="${CSS.escape(id)}"]`);
                        if (liveBubble) {
                            const wrap = liveBubble.closest('.bubble-wrap');
                            const tmp  = document.createElement('div');
                            tmp.innerHTML = buildBubble(msgs[idx], myUsername);
                            const newBubble = tmp.firstElementChild;
                            newBubble.querySelectorAll('[data-action]').forEach(b => b.addEventListener('click', () => handleAction(b, body, currentConvMap, myUsername)));
                            newBubble.querySelectorAll('[data-copy]').forEach(b => b.addEventListener('click', () => navigator.clipboard.writeText(b.dataset.copy)));
                            (wrap ?? liveBubble).replaceWith(newBubble);
                        }
                    }
                }
            }

            for (const msg of newDecrypted) {
                knownIds.add(String(msg.id ?? msg.message_id ?? ''));
                const partner = msg.direction === 'sent'
                    ? (msg.recipient_username ?? 'Unknown')
                    : (msg.sender_username ?? 'Unknown');
                if (!currentConvMap.has(partner)) currentConvMap.set(partner, []);
                currentConvMap.get(partner).push(msg);
            }

            const partnerEl     = body.querySelector('.thread-partner');
            const activePartner = partnerEl?.textContent?.trim() ?? '';
            renderConvList(currentConvMap, activePartner);

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
                        btn.addEventListener('click', () => handleAction(btn, body, currentConvMap, myUsername));
                    });
                    bubble.querySelectorAll('[data-copy]').forEach(btn => {
                        btn.addEventListener('click', () => navigator.clipboard.writeText(btn.dataset.copy));
                    });
                    thread.appendChild(bubble);
                }
                if (relevant.length > 0) thread.scrollTop = thread.scrollHeight;
            }
        } catch { /* non-fatal */ }
    }, 10_000);
}

// ── Bubble renderer ───────────────────────────────────────────────────────
function buildBubble(msg, myUsername) {
    const id        = msg.id ?? msg.message_id ?? '';
    const isSent    = msg.direction === 'sent' || (msg.sender_username === myUsername);
    const isRevoked = !!msg.is_revoked;
    const content   = msg.content ?? '(encrypted)';
    const date      = formatDate(msg.created_at ?? msg.timestamp);
    const sender    = msg.sender_username ?? 'Unknown';
    const sid       = esc(String(id));

    const avatarLabel = isSent
        ? esc((myUsername[0] ?? '?').toUpperCase())
        : esc((sender[0] ?? '?').toUpperCase());

    const actions = `
        ${!isRevoked ? `<button class="btn-icon" data-action="forward"  data-id="${sid}" title="Forward">↗️</button>` : ''}
        <button class="btn-icon" data-action="download" data-id="${sid}" title="Download">⬇️</button>
        ${!isSent && !isRevoked && !msg.tx_hash ? `<button class="btn-icon" data-action="anchor" data-id="${sid}" title="Anchor to blockchain">⚓</button>` : ''}
        ${isSent ? `<button class="btn-icon" data-action="delete" data-id="${sid}" title="Delete">🗑️</button>` : ''}
        ${isSent && !isRevoked ? `<button class="btn-revoke" data-action="revoke" data-id="${sid}" title="Revoke access">🚫 Revoke</button>` : ''}
        ${isRevoked ? `<span class="revoked-badge">Revoked</span>` : ''}`;

    const anchorBadge = msg.tx_hash
        ? `<span class="badge badge-anchored">&#9875; ${esc(String(msg.tx_hash).slice(0, 10))}</span>`
        : `<span class="badge badge-unanchored">Not anchored</span>`;
    const hashRows = (msg.tx_hash || msg.merkle_root) ? `
            <div class="msg-hash-rows">
                ${msg.tx_hash     ? `<div class="msg-hash-row"><span class="msg-hash-label">TX:</span><code>${esc(String(msg.tx_hash))}</code><button class="btn-copy" data-copy="${esc(String(msg.tx_hash))}" title="Copy">&#128203;</button></div>` : ''}
                ${msg.merkle_root ? `<div class="msg-hash-row"><span class="msg-hash-label">Root:</span><code>${esc(String(msg.merkle_root))}</code><button class="btn-copy" data-copy="${esc(String(msg.merkle_root))}" title="Copy">&#128203;</button></div>` : ''}
            </div>` : '';

    return `
        <div class="bubble-wrap ${isSent ? 'sent' : 'received'}${isRevoked ? ' revoked' : ''}">
            <div class="bubble-avatar">${avatarLabel}</div>
            <div class="bubble ${isSent ? 'sent' : 'received'} message-card" data-id="${esc(String(id))}" data-sender="${esc(sender)}">
                <div class="msg-body">${esc(String(content))}</div>
                ${hashRows}
                <div class="bubble-footer">
                    ${date ? `<span class="msg-date">${date}</span>` : '<span></span>'}
                    ${anchorBadge}
                    <div class="msg-actions">${actions}</div>
                </div>
            </div>
        </div>`;
}

// ── Forward dialog ────────────────────────────────────────────────────────
function showForwardDialog() {
    return new Promise(resolve => {
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

        input.value = '';
        fpEl.textContent = '';
        msgEl.className = msgEl.textContent = '';

        const fwdAbort = new AbortController();

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
        }, { signal: fwdAbort.signal });

        cancelBtn.onclick = () => { fwdAbort.abort(); dlg.close(); resolve(null); };

        form.onsubmit = (e) => {
            e.preventDefault();
            const username = input.value.trim();
            if (!username) {
                msgEl.className = 'error-msg';
                msgEl.textContent = 'Please enter a username.';
                return;
            }
            fwdAbort.abort();
            dlg.close();
            resolve(username);
        };

        dlg.showModal();
        input.focus();
    });
}

// ── New Chat dialog ───────────────────────────────────────────────────────
function showNewChatDialog() {
    return new Promise(resolve => {
        let dlg = document.getElementById('new-chat-dialog');
        if (!dlg) {
            dlg = document.createElement('dialog');
            dlg.id = 'new-chat-dialog';
            dlg.innerHTML = `
                <form id="new-chat-form" method="dialog" novalidate>
                    <h3 style="margin-bottom:1.25rem">New chat</h3>
                    <div class="form-group">
                        <label for="nc-username">Username</label>
                        <input type="text" id="nc-username" autocomplete="off" placeholder="Enter username" required>
                    </div>
                    <div style="display:flex;gap:.75rem;margin-top:1rem">
                        <button type="submit" class="btn btn-primary">Start chat</button>
                        <button type="button" class="btn btn-secondary" id="nc-cancel">Cancel</button>
                    </div>
                    <div id="nc-msg" role="alert" style="margin-top:.5rem"></div>
                </form>`;
            document.body.appendChild(dlg);
        }

        const form      = dlg.querySelector('#new-chat-form');
        const input     = dlg.querySelector('#nc-username');
        const msgEl     = dlg.querySelector('#nc-msg');
        const cancelBtn = dlg.querySelector('#nc-cancel');

        input.value = '';
        msgEl.className = msgEl.textContent = '';

        const abort = new AbortController();

        form.addEventListener('submit', e => {
            e.preventDefault();
            const val = input.value.trim();
            if (!val) {
                msgEl.className = 'error-msg';
                msgEl.textContent = 'Please enter a username.';
                return;
            }
            abort.abort();
            dlg.close();
            resolve(val);
        }, { signal: abort.signal });

        cancelBtn.addEventListener('click', () => {
            abort.abort();
            dlg.close();
            resolve(null);
        }, { signal: abort.signal });

        dlg.addEventListener('cancel', () => { abort.abort(); resolve(null); }, { signal: abort.signal });

        dlg.showModal();
        input.focus();
    });
}

// ── Key-changed warning dialog ────────────────────────────────────────────
function showKeyChangedDialog(partner) {
    return new Promise(resolve => {
        let dlg = document.getElementById('key-changed-dialog');
        if (!dlg) {
            dlg = document.createElement('dialog');
            dlg.id = 'key-changed-dialog';
            dlg.innerHTML = `
                <div style="max-width:26rem">
                    <h3 style="margin-bottom:.75rem">⚠️ Encryption key changed</h3>
                    <p id="kc-body" style="margin-bottom:1rem;font-size:.9rem;line-height:1.5"></p>
                    <div class="warning-msg" style="margin-bottom:1rem;font-size:.85rem">
                        This could indicate a key rotation or a man-in-the-middle attack.
                        Verify their fingerprint out-of-band before continuing.
                    </div>
                    <div style="display:flex;gap:.75rem">
                        <button class="btn btn-primary" id="kc-proceed">Send anyway</button>
                        <button class="btn btn-secondary" id="kc-cancel">Cancel</button>
                    </div>
                </div>`;
            document.body.appendChild(dlg);
        }

        dlg.querySelector('#kc-body').textContent =
            `${partner}'s encryption key has changed since your last conversation.`;

        const abort = new AbortController();

        dlg.querySelector('#kc-proceed').addEventListener('click', () => {
            abort.abort(); dlg.close(); resolve(true);
        }, { signal: abort.signal });

        dlg.querySelector('#kc-cancel').addEventListener('click', () => {
            abort.abort(); dlg.close(); resolve(false);
        }, { signal: abort.signal });

        dlg.addEventListener('cancel', () => { abort.abort(); resolve(false); }, { signal: abort.signal });

        dlg.showModal();
    });
}

// ── Message action handler ────────────────────────────────────────────────
async function handleAction(btn, inboxBody, currentConvMap, myUsername) {
    const { action, id } = btn.dataset;
    btn.disabled = true;

    try {
        switch (action) {
            case 'delete': {
                await api.deleteMessage(id);
                (btn.closest('.bubble-wrap') ?? btn.closest('.message-card'))?.remove();
                // remove from map so sidebar preview and thread reopens stay consistent
                for (const [partner, msgs] of currentConvMap) {
                    const idx = msgs.findIndex(m => String(m.id ?? m.message_id ?? '') === id);
                    if (idx !== -1) {
                        msgs.splice(idx, 1);
                        if (msgs.length === 0) currentConvMap.delete(partner);
                        renderConvList(currentConvMap, inboxBody.querySelector('.thread-partner')?.textContent?.trim() ?? '');
                        break;
                    }
                }
                break;
            }

            case 'revoke': {
                await api.revokeMessage(id);
                // mark revoked in map so sidebar and thread reopens show "Revoked" state
                for (const msgs of currentConvMap.values()) {
                    const entry = msgs.find(m => String(m.id ?? m.message_id ?? '') === id);
                    if (entry) { entry.is_revoked = true; break; }
                }
                // replace bubble in-place with revoked rendering
                const card = btn.closest('.message-card');
                const wrap = btn.closest('.bubble-wrap') ?? card;
                const entry = (() => {
                    for (const msgs of currentConvMap.values()) {
                        const e = msgs.find(m => String(m.id ?? m.message_id ?? '') === id);
                        if (e) return e;
                    }
                })();
                if (entry && wrap) {
                    const tmp = document.createElement('div');
                    tmp.innerHTML = buildBubble(entry, myUsername);
                    const newBubble = tmp.firstElementChild;
                    newBubble.querySelectorAll('[data-action]').forEach(b => b.addEventListener('click', () => handleAction(b, inboxBody, currentConvMap, myUsername)));
                    wrap.replaceWith(newBubble);
                } else {
                    wrap?.remove();
                }
                renderConvList(currentConvMap, inboxBody.querySelector('.thread-partner')?.textContent?.trim() ?? '');
                break;
            }

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

            case 'anchor': {
                const origLabel = btn.textContent;
                btn.textContent = '⏳';
                try {
                    await api.triggerAnchor();
                } catch (err) {
                    if (err.status === 503) {
                        showInlineError(inboxBody, 'Anchoring is not enabled on this server.');
                    } else {
                        throw err;
                    }
                    btn.disabled = false;
                    btn.textContent = origLabel;
                    return;
                }
                btn.textContent = '✅';
                setTimeout(() => { btn.disabled = false; btn.textContent = origLabel; }, 2000);
                return;
            }

            case 'forward': {
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
                    'raw', origEphPkBytes, { name: 'X25519' }, false, [],
                );
                const origSenderKeyBytes = Uint8Array.from(atob(orig.sender_x25519_public_key), c => c.charCodeAt(0));
                const origSenderPubKey   = await crypto.subtle.importKey('raw', origSenderKeyBytes, { name: 'X25519' }, false, []);

                const origCt    = decodeField(orig.ciphertext);
                const origNonce = decodeField(orig.nonce);
                const origTs    = parseTimestamp(orig.created_at);
                const plaintext = await decryptMessage(
                    origCt, origNonce, origEphPubKey, origEphPkBytes,
                    privKey, origSenderPubKey,
                    orig.sender_id ?? '', orig.recipient_id ?? '', orig.id, origTs,
                );

                const recipKeyBytes  = Uint8Array.from(atob(recipientUser.x25519_public_key), c => c.charCodeAt(0));
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

                sessionStorage.setItem(`sent_plain_${messageId}`, plaintext);

                const origLabel = btn.textContent;
                btn.textContent = '✅';
                setTimeout(() => { btn.disabled = false; btn.textContent = origLabel; }, 1500);
                return;
            }
        }

        if (!inboxBody.querySelector('.message-card')) {
            inboxBody.innerHTML = `<div class="empty-state">Your inbox is empty.</div>`;
        }
    } catch (err) {
        showInlineError(inboxBody, `Action failed: ${err.message}`);
        btn.disabled = false;
    }
}
