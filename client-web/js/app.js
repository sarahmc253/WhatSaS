/**
 * app.js — entry point.
 *
 * Owns the hash-based router and wires the persistent navbar.
 * All view rendering is delegated to views.js.
 */

import * as api   from './api.js';
import { renderLogin, renderUnlock } from './views/auth.js';
import { renderInbox } from './views/inbox.js';
import { renderVerify } from '../blockchain/blockchainVerifyView.js';

const appEl   = document.getElementById('app');
const navbar  = document.getElementById('navbar');
const footer  = document.getElementById('site-footer');

// ── Navbar buttons ────────────────────────────────────────────────────────
document.getElementById('nav-inbox').addEventListener('click',  () => navigate('inbox'));
document.getElementById('nav-verify').addEventListener('click', () => navigate('verify'));
document.getElementById('nav-change-password').addEventListener('click', () => {
    // Dispatch a custom event that renderInbox listens for
    document.dispatchEvent(new CustomEvent('open-change-password'));
});
document.getElementById('nav-logout').addEventListener('click', () => {
    api.logout();
    navigate('login');
});

// ── Router ────────────────────────────────────────────────────────────────
export function navigate(view) {
    location.hash = view;
}

async function route() {
    const view = location.hash.slice(1) || 'login';

    // Redirect unauthenticated users away from protected views
    // verify is public — no login required to check a message's on-chain record
    const PUBLIC_VIEWS = new Set(['login', 'verify']);
    const isAuthView   = view === 'login' || view === 'verify';

    // Apply layout class immediately — before any early returns — so there's
    // never a frame where the chat full-width layout clips auth content.
    navbar.hidden = !api.isAuthenticated() || isAuthView;
    appEl.classList.toggle('auth-mode', isAuthView);

    if (!api.isAuthenticated() && !PUBLIC_VIEWS.has(view)) {
        navigate('login');
        return;
    }
    if (api.isAuthenticated() && !api.getPrivateKey() && !PUBLIC_VIEWS.has(view)) {
        // Token is valid but private key was lost on page reload — show unlock prompt
        // rather than forcing a full re-login.
        appEl.classList.add('auth-mode');
        renderUnlock(appEl, navigate, () => route());
        return;
    }

    switch (view) {
        case 'login':
            renderLogin(appEl, navigate);
            break;
        case 'inbox':
            await renderInbox(appEl, navigate);
            break;
        case 'verify':
            renderVerify(appEl);
            break;
        default:
            navigate(api.isAuthenticated() ? 'inbox' : 'login');
    }
}

window.addEventListener('hashchange', route);
route(); // render on initial page load
