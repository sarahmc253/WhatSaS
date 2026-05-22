/**
 * app.js — entry point.
 *
 * Owns the hash-based router and wires the persistent navbar.
 * All view rendering is delegated to views.js.
 */

import * as api   from './api.js';
import { renderLogin, renderInbox, renderCompose } from './views.js';

const appEl   = document.getElementById('app');
const navbar  = document.getElementById('navbar');

// ── Navbar buttons ────────────────────────────────────────────────────────
document.getElementById('nav-inbox').addEventListener('click',   () => navigate('inbox'));
document.getElementById('nav-compose').addEventListener('click', () => navigate('compose'));
document.getElementById('nav-logout').addEventListener('click',  () => {
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
    if (!api.isAuthenticated() && view !== 'login') {
        navigate('login');
        return;
    }

    navbar.hidden = !api.isAuthenticated();

    switch (view) {
        case 'login':
            renderLogin(appEl, navigate);
            break;
        case 'inbox':
            await renderInbox(appEl, navigate);
            break;
        case 'compose':
            renderCompose(appEl, navigate);
            break;
        default:
            navigate(api.isAuthenticated() ? 'inbox' : 'login');
    }
}

window.addEventListener('hashchange', route);
route(); // render on initial page load
