/**
 * blockchainVerifyView.js — Blockchain verification view.
 *
 * Publicly accessible at #verify (no authentication required).
 * Uses ethers.js v6 UMD loaded globally as window.ethers.
 *
 * Flow:
 *   Step 1 — User pastes a Sepolia tx hash → "Fetch from chain"
 *   Step 2 — On-chain hash + timestamp displayed; user pastes original content
 *   Step 3 — "Verify integrity" → keccak256 comparison → PASS / FAIL
 *
 * Spec: blockchain verification requirement — prove a message was anchored on-chain
 * and has not been altered since anchoring.
 *
 * How keccak256 matching works:
 *   - On-chain: storeData(string data) → contract stores keccak256(abi.encodePacked(data))
 *   - Client:   ethers.keccak256(ethers.toUtf8Bytes(content)) — identical encoding
 *   - dataHash is an *indexed* bytes32 topic, so ethers decodes it from topics[2], not args
 */

// ABI fragments — event for log parsing to extract recordId; getRecord to fetch hash+timestamp
const DATA_STORED_EVENT_ABI = [
    'event DataStored(uint256 indexed recordId, bytes32 indexed dataHash, uint256 timestamp, address indexed recorder)',
];
const GET_RECORD_ABI = [
    'function getRecord(uint256 recordId) view returns (bytes32 hash, uint256 timestamp, address recorder)',
];

// ── XSS helper ────────────────────────────────────────────────────────────
function esc(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#x27;');
}

function formatDate(unixSeconds) {
    const d = new Date(Number(unixSeconds) * 1000);
    return isNaN(d.getTime()) ? 'Unknown date' : d.toLocaleString();
}

// ── Config ────────────────────────────────────────────────────────────────
// Contract address and RPC URL come from <meta> tags in index.html —
// single place to update per deployment.
function getConfig() {
    const address = document.querySelector('meta[name="contract-address"]')?.content?.trim();
    const rpcUrl  = document.querySelector('meta[name="sepolia-rpc-url"]')?.content?.trim();

    if (!address || !/^0x[0-9a-fA-F]{40}$/.test(address)) {
        throw new Error(
            'Contract address is not configured. Set <meta name="contract-address" content="0x…"> in index.html.'
        );
    }
    if (!rpcUrl || !rpcUrl.startsWith('http')) {
        throw new Error(
            'Sepolia RPC URL is not configured. Set <meta name="sepolia-rpc-url" content="https://…"> in index.html.'
        );
    }

    return { address, rpcUrl };
}

// ── Render ─────────────────────────────────────────────────────────────────
export function renderVerify(container) {
    container.innerHTML = `
        <div class="verify-header">
            <h2>Blockchain Verification</h2>
            <p class="verify-subtitle">
                Verify that a message was recorded on the Sepolia blockchain and has not been altered.
            </p>
        </div>

        <div class="card">
            <!-- Step 1: tx hash input -->
            <div id="step-fetch">
                <div class="form-group">
                    <label for="v-txhash">Transaction hash</label>
                    <input type="text" id="v-txhash"
                           placeholder="0x…"
                           autocomplete="off"
                           spellcheck="false">
                </div>
                <button class="btn btn-primary" id="btn-fetch">Fetch from chain</button>
                <div id="fetch-status" role="alert" style="margin-top:.75rem"></div>
            </div>

            <!-- Step 2: on-chain info + content input (hidden until fetch succeeds) -->
            <div id="step-verify" hidden>
                <hr style="border:none; border-top:1px solid var(--border); margin:1.5rem 0">

                <div class="verify-onchain-info" id="onchain-info"></div>

                <div class="form-group" style="margin-top:1.5rem">
                    <label for="v-content">Original message content</label>
                    <textarea id="v-content"
                              placeholder="Paste the original message text exactly as stored — whitespace matters."
                              style="min-height:140px; font-family:monospace; font-size:.875rem;"></textarea>
                </div>
                <div class="compose-actions">
                    <button class="btn btn-primary" id="btn-verify">Verify integrity</button>
                    <button class="btn btn-secondary" id="btn-reset">Start over</button>
                </div>
                <div id="verify-result" role="alert" style="margin-top:.75rem"></div>
            </div>
        </div>`;

    // Wire all listeners once — Step 2 buttons are present in the DOM already,
    // just hidden. handleVerify/handleReset guard against missing state.
    document.getElementById('btn-fetch').addEventListener('click', handleFetch);
    document.getElementById('v-txhash').addEventListener('keydown', (e) => {
        if (e.key === 'Enter') handleFetch();
    });
    document.getElementById('btn-verify').addEventListener('click', handleVerify);
    document.getElementById('btn-reset').addEventListener('click', handleReset);
}

// ── Step 1: fetch tx from chain ────────────────────────────────────────────
async function handleFetch() {
    const txInput     = document.getElementById('v-txhash');
    const statusEl    = document.getElementById('fetch-status');
    const fetchBtn    = document.getElementById('btn-fetch');
    const stepVerify  = document.getElementById('step-verify');
    const onchainInfo = document.getElementById('onchain-info');

    const txHash = txInput.value.trim();

    if (!/^0x[0-9a-fA-F]{64}$/.test(txHash)) {
        setStatus(statusEl, 'error', 'Enter a valid 66-character hex transaction hash (starting with 0x).');
        return;
    }

    // Clear previous fetch result before starting a new one
    window._verifyState = null;
    stepVerify.hidden = true;
    onchainInfo.innerHTML = '';
    const verifyResult = document.getElementById('verify-result');
    verifyResult.className = '';
    verifyResult.innerHTML = '';
    document.getElementById('v-content').value = '';

    setStatus(statusEl, 'loading', 'Querying Sepolia…');
    fetchBtn.disabled = true;

    try {
        // Check ethers before anything else so the error is clear
        if (!window.ethers) {
            throw new Error('ethers.js failed to load. Check your internet connection and reload the page.');
        }

        const { address: contractAddress, rpcUrl } = getConfig();
        const provider = new window.ethers.JsonRpcProvider(rpcUrl);
        const iface    = new window.ethers.Interface(DATA_STORED_EVENT_ABI);

        // Null if tx is unknown or not yet confirmed
        const receipt = await provider.getTransactionReceipt(txHash);
        if (!receipt) {
            throw new Error('Transaction not found on Sepolia. Check the hash and wait for confirmation if the tx is recent.');
        }

        if (receipt.status === 0) {
            throw new Error('This transaction was reverted. No data was stored on-chain.');
        }

        // Parse the event log to extract the recordId emitted by storeData()
        const eventLog = parseDataStoredEvent(iface, receipt.logs, contractAddress);
        if (!eventLog) {
            throw new Error(
                'No DataStored event found in this transaction. ' +
                'Make sure this is a WhatSaS storeData transaction on the correct contract.'
            );
        }

        // Call getRecord(recordId) — the authoritative on-chain read for the
        // keccak256 hash and timestamp stored at that record slot.
        const contract = new window.ethers.Contract(contractAddress, GET_RECORD_ABI, provider);
        let record;
        try {
            record = await contract.getRecord(eventLog.recordId);
        } catch {
            throw new Error(
                'getRecord() call failed. The contract at the configured address may not support this function, ' +
                'or the record ID does not exist.'
            );
        }
        const onchainHash = record.hash;       // bytes32 hex string
        const timestamp   = record.timestamp;  // BigInt (Unix seconds)
        const recorder    = record.recorder;   // address string

        window._verifyState = {
            onchainHash,
            recordId: eventLog.recordId.toString(),
            recorder,
            timestamp,
        };

        onchainInfo.innerHTML = `
            <div class="verify-info-row">
                <span class="verify-info-label">Record ID</span>
                <span class="verify-info-value">${esc(eventLog.recordId.toString())}</span>
            </div>
            <div class="verify-info-row">
                <span class="verify-info-label">On-chain hash</span>
                <code class="verify-hash">${esc(onchainHash)}</code>
            </div>
            <div class="verify-info-row">
                <span class="verify-info-label">Recorded by</span>
                <code class="verify-hash">${esc(recorder)}</code>
            </div>
            <div class="verify-info-row">
                <span class="verify-info-label">Timestamp</span>
                <span class="verify-info-value">${esc(formatDate(timestamp))}</span>
            </div>`;

        setStatus(statusEl, 'success', 'Record found on-chain.');
        stepVerify.hidden = false;

    } catch (err) {
        setStatus(statusEl, 'error', err.message || 'Network error — check your connection and try again.');
    } finally {
        fetchBtn.disabled = false;
    }
}

// ── Step 2: verify content hash ────────────────────────────────────────────
function handleVerify() {
    const content  = document.getElementById('v-content').value;
    const resultEl = document.getElementById('verify-result');
    const state    = window._verifyState;

    if (!state) {
        setStatus(resultEl, 'error', 'Verification state lost — please fetch the transaction again.');
        return;
    }

    if (content === '') {
        setStatus(resultEl, 'error', 'Paste the original message content before verifying.');
        return;
    }

    // Do NOT trim — keccak256 is sensitive to every character including whitespace.
    // keccak256(toUtf8Bytes(s)) === Solidity keccak256(abi.encodePacked(string s))
    const computedHash = window.ethers.keccak256(window.ethers.toUtf8Bytes(content));
    const match = computedHash.toLowerCase() === state.onchainHash.toLowerCase();

    if (match) {
        resultEl.className = 'success-msg';
        resultEl.innerHTML =
            `<strong>PASS</strong> — Message integrity verified.<br>
            Recorded on-chain at <strong>${esc(formatDate(state.timestamp))}</strong>.<br>
            <span class="verify-hash-line">Hash: <code class="verify-hash">${esc(computedHash)}</code></span>`;
    } else {
        resultEl.className = 'error-msg';
        resultEl.innerHTML =
            `<strong>FAIL</strong> — Hash mismatch. The content does not match the on-chain record.<br>
            <span class="verify-hash-line">Expected:&nbsp;<code class="verify-hash">${esc(state.onchainHash)}</code></span><br>
            <span class="verify-hash-line">Computed:&nbsp;<code class="verify-hash">${esc(computedHash)}</code></span>`;
    }
}

// ── Reset ──────────────────────────────────────────────────────────────────
function handleReset() {
    document.getElementById('v-txhash').value  = '';
    document.getElementById('v-content').value = '';

    const fetchStatus = document.getElementById('fetch-status');
    fetchStatus.className = '';
    fetchStatus.innerHTML = '';

    const verifyResult = document.getElementById('verify-result');
    verifyResult.className = '';
    verifyResult.innerHTML = '';

    document.getElementById('step-verify').hidden = true;
    window._verifyState = null;
}

// ── Helpers ────────────────────────────────────────────────────────────────

// Returns the recordId from the first DataStored log emitted by our contract.
// recordId is all we need here — hash and timestamp are fetched via getRecord().
function parseDataStoredEvent(iface, logs, contractAddress) {
    for (const log of logs) {
        if (log.address.toLowerCase() !== contractAddress.toLowerCase()) continue;
        try {
            const parsed = iface.parseLog({ topics: log.topics, data: log.data });
            if (parsed && parsed.name === 'DataStored') {
                return { recordId: parsed.args.recordId }; // BigInt
            }
        } catch {
            // Different event or contract — skip
        }
    }
    return null;
}

function setStatus(el, type, message) {
    if (type === 'loading') {
        el.className = '';
        el.innerHTML = `<div class="loading" style="justify-content:flex-start;padding:.5rem 0">
            <span class="spinner"></span> ${esc(message)}
        </div>`;
    } else {
        el.className = type === 'error' ? 'error-msg' : 'success-msg';
        el.textContent = message;
    }
}
