import * as api from '../api.js';
import { generateKeypair, getPublicKeyBytes, getPrivateKeyBytes } from '../../crypto/keypair.js';
import { encryptPrivateKey, decryptPrivateKey, EncryptedPrivateKey } from '../../crypto/keyStorage.js';
import { esc, keyFingerprint, validatePassword } from './helpers.js';

const BEAR_SVG = `<div class="bear-emoji" aria-hidden="true">🧸</div>`;

function teddyShell(formHTML) {
    return `
    <div class="auth-wrap">
        ${BEAR_SVG}
        <div class="auth-card">
            ${formHTML}
        </div>
    </div>`;
}

// ── Unlock view ───────────────────────────────────────────────────────────────
export function renderUnlock(container, navigate, onUnlocked) {
    const username = api.getUsername() ?? '';
    container.innerHTML = teddyShell(`
        <h1>Welcome back!</h1>
        <p class="auth-sub">Your session is still active${username ? ` as <strong>${esc(username)}</strong>` : ''}.</p>
        <form id="unlock-form" novalidate>
            <div class="form-group">
                <label for="u-password">Password</label>
                <input type="password" id="u-password" autocomplete="current-password" required autofocus placeholder="Enter your password">
            </div>
            <button type="submit" class="btn btn-primary" style="width:100%">Unlock</button>
            <div id="unlock-msg" role="alert"></div>
        </form>
        <div class="auth-toggle">
            Not you? <button id="unlock-logout">Sign in as someone else</button>
        </div>
    `);

    document.getElementById('unlock-logout').addEventListener('click', () => {
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

        const password   = document.getElementById('u-password').value;
        const wrappedB64 = api.getStoredWrappedKey();

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
                'pkcs8', privBytes, { name: 'X25519' }, false, ['deriveBits'],
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

// ── Login view ────────────────────────────────────────────────────────────────
export function renderLogin(container, navigate) {
    container.innerHTML = teddyShell(`
        <h1>Welcome back!</h1>
        <form id="login-form" novalidate>
            <div class="form-group">
                <label for="l-username">Username</label>
                <input type="text" id="l-username" autocomplete="username" required placeholder="your_username">
            </div>
            <div class="form-group">
                <label for="l-password">Password</label>
                <input type="password" id="l-password" autocomplete="current-password" required placeholder="••••••••">
            </div>
            <div id="login-msg" role="alert"></div>
            <button type="submit" class="btn btn-primary" style="width:100%">Sign in</button>
        </form>
        <div class="auth-toggle">
            No account yet? <button id="show-register">Register</button>
        </div>
    `);

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

// ── Register view ─────────────────────────────────────────────────────────────
function renderRegister(container, navigate) {
    container.innerHTML = teddyShell(`
        <h1>Create account</h1>
        <form id="reg-form" novalidate>
            <div class="reg-row">
                <div class="form-group">
                    <label for="r-username">Username</label>
                    <input type="text" id="r-username" autocomplete="username" required
                           pattern="[A-Za-z0-9_]{3,32}" placeholder="your_username">
                    <div class="pw-rules"><span id="username-rule">✗ 3–32 chars, a-z/0-9/_</span></div>
                </div>
                <div class="form-group">
                    <label for="r-email">Email</label>
                    <input type="email" id="r-email" autocomplete="email" required placeholder="bear@woods.com">
                </div>
            </div>
            <div class="form-group">
                <label for="r-password">Password</label>
                <input type="password" id="r-password" autocomplete="new-password" required placeholder="••••••••">
                <div id="pw-rules" class="pw-rules">
                    <span data-rule="length">✗ 8+ chars</span>
                    <span data-rule="upper">✗ Uppercase</span>
                    <span data-rule="lower">✗ Lowercase</span>
                    <span data-rule="number">✗ Number</span>
                    <span data-rule="special">✗ Special char</span>
                </div>
            </div>
            <div class="form-group" id="r-confirm-group" hidden>
                <label for="r-confirm">Confirm password</label>
                <input type="password" id="r-confirm" autocomplete="new-password" placeholder="••••••••">
                <div class="pw-rules"><span id="confirm-match-rule">✗ Passwords must match</span></div>
            </div>
            <div id="reg-msg" role="alert"></div>
            <button type="submit" class="btn btn-primary" style="width:100%">Create account</button>
        </form>
        <div class="auth-toggle">
            Already have an account? <button id="show-login">Sign in</button>
        </div>
    `);

    const form    = document.getElementById('reg-form');
    const msg     = document.getElementById('reg-msg');
    const pwInput = document.getElementById('r-password');
    const unInput = document.getElementById('r-username');

    unInput.addEventListener('input', () => {
        const el = document.getElementById('username-rule');
        if (!el) return;
        const ok = /^[A-Za-z0-9_]{3,32}$/.test(unInput.value);
        el.textContent = (ok ? '✓ ' : '✗ ') + '3–32 chars, letters/numbers/underscores only';
        el.classList.toggle('pw-rule-ok', ok);
    });

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
        document.getElementById('r-confirm-group').hidden = pw.length === 0;
        updateConfirmIndicator();
    });

    function updateConfirmIndicator() {
        const el = document.getElementById('confirm-match-rule');
        if (!el) return;
        const confirm = document.getElementById('r-confirm').value;
        if (!confirm) { el.textContent = '✗ Passwords must match'; el.classList.remove('pw-rule-ok'); return; }
        const ok = pwInput.value === confirm;
        el.textContent = (ok ? '✓ ' : '✗ ') + 'Passwords must match';
        el.classList.toggle('pw-rule-ok', ok);
    }

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const btn = form.querySelector('button[type="submit"]');
        btn.disabled = true;
        msg.className = msg.textContent = '';

        try {
            const username = unInput.value.trim();
            const email    = document.getElementById('r-email').value.trim();
            const password = pwInput.value;
            const confirm  = document.getElementById('r-confirm').value;

            if (!/^[A-Za-z0-9_]{3,32}$/.test(username)) {
                msg.className = 'error-msg';
                msg.textContent = 'Username must be 3–32 characters: letters, numbers, and underscores only.';
                btn.disabled = false;
                return;
            }

            const pwErrors = validatePassword(password);
            if (pwErrors.length > 0) {
                msg.className = 'error-msg';
                msg.textContent = `Password must contain: ${pwErrors.join(', ')}.`;
                btn.disabled = false;
                return;
            }

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
                x25519_public_key:   toB64(pubKeyBytes),
                wrapped_private_key: btoa(JSON.stringify(encryptedPrivateKey.toJSON())),
                kek_salt:            toB64(encryptedPrivateKey.salt),
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

    document.getElementById('r-confirm').addEventListener('input', updateConfirmIndicator);
    document.getElementById('show-login').addEventListener('click', () => renderLogin(container, navigate));
}
