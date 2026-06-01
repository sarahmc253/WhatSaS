import { generateKeypair, getPublicKeyBytes } from './keypair.js';
import { HKDF_INFO_MSG_ENC } from './constants.js';

// ── HKDF helpers matching C++ hkdfExtract / hkdfExpand32 ─────────────────────

// HKDF-Extract: PRK = HMAC-SHA256(key=salt, data=ikm)
async function hkdfExtract(salt, ikm) {
    const key = await crypto.subtle.importKey('raw', salt, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    return new Uint8Array(await crypto.subtle.sign('HMAC', key, ikm));
}

// HKDF-Expand single block: OKM = HMAC-SHA256(key=prk, data=info‖0x01)
async function hkdfExpand32(prk, info) {
    const key = await crypto.subtle.importKey('raw', prk, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    const infoBytes = new TextEncoder().encode(info);
    const data = new Uint8Array(infoBytes.length + 1);
    data.set(infoBytes);
    data[infoBytes.length] = 0x01;
    return new Uint8Array(await crypto.subtle.sign('HMAC', key, data));
}

// Derive AES-256-GCM key from two DH outputs. Matches C++ hpkeSend / hpkeReceive:
//   IKM = dh1 ‖ dh2,  PRK = HKDF-Extract(salt=ephPkBytes, IKM),  KEY = HKDF-Expand(PRK)
async function deriveAesKey(dh1, dh2, ephPkBytes, usage) {
    const ikm = new Uint8Array(64);
    ikm.set(dh1, 0);
    ikm.set(dh2, 32);
    const prk      = await hkdfExtract(ephPkBytes, ikm);
    const keyBytes = await hkdfExpand32(prk, HKDF_INFO_MSG_ENC);
    return crypto.subtle.importKey('raw', keyBytes, { name: 'AES-GCM' }, false, [usage]);
}

// Canonical AD JSON — key order must match C++ buildAd() (nlohmann::ordered_json, no spaces).
function buildAd(senderId, recipientId, messageId, timestamp) {
    return JSON.stringify({ sender_id: senderId, recipient_id: recipientId, message_id: messageId, timestamp });
}

// 32-char lowercase hex message ID — matches C++ generateMsgId().
function generateMsgId() {
    return Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('');
}

// ── Public API ────────────────────────────────────────────────────────────────

export async function encryptMessage(plaintext, recipientPublicKey, senderPrivateKey, senderId, recipientId) {
    let ephemeralPublicKey, ephemeralPrivateKey;
    try {
        ({ publicKey: ephemeralPublicKey, privateKey: ephemeralPrivateKey } = await generateKeypair());
    } catch (err) {
        console.error('encryptMessage: generateKeypair() threw:', err);
        throw err;
    }
    const ephPkBytes = await getPublicKeyBytes(ephemeralPublicKey);
    const messageId  = generateMsgId();
    const timestamp  = Math.floor(Date.now() / 1000);

    // DH1 = X25519(eph_sk, recipient_pk),  DH2 = X25519(sender_sk, recipient_pk)
    let dh1;
    try {
        dh1 = new Uint8Array(await crypto.subtle.deriveBits({ name: 'X25519', public: recipientPublicKey }, ephemeralPrivateKey, 256));
    } catch (err) {
        console.error('encryptMessage: deriveBits DH1 (ephemeralPrivateKey × recipientPublicKey) threw:', err);
        throw err;
    }
    let dh2;
    try {
        dh2 = new Uint8Array(await crypto.subtle.deriveBits({ name: 'X25519', public: recipientPublicKey }, senderPrivateKey, 256));
    } catch (err) {
        console.error('encryptMessage: deriveBits DH2 (senderPrivateKey × recipientPublicKey) threw:', err);
        throw err;
    }

    const messageKey = await deriveAesKey(dh1, dh2, ephPkBytes, 'encrypt');
    const ad    = new TextEncoder().encode(buildAd(senderId, recipientId, messageId, timestamp));
    const nonce = crypto.getRandomValues(new Uint8Array(12));

    const ciphertext = new Uint8Array(await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv: nonce, additionalData: ad },
        messageKey,
        new TextEncoder().encode(plaintext),
    ));

    return { ephemeralPublicKey, ephPkBytes, nonce, ciphertext, messageId, timestamp };
}

export async function decryptMessage(
    ciphertext, nonce, ephPublicKey, ephPkBytes,
    recipientPrivateKey, senderStaticPubKey,
    senderId, recipientId, messageId, timestamp,
) {
    // DH1 = X25519(recipient_sk, eph_pk),  DH2 = X25519(recipient_sk, sender_pk)
    const dh1 = new Uint8Array(await crypto.subtle.deriveBits({ name: 'X25519', public: ephPublicKey }, recipientPrivateKey, 256));
    const dh2 = new Uint8Array(await crypto.subtle.deriveBits({ name: 'X25519', public: senderStaticPubKey }, recipientPrivateKey, 256));

    const messageKey = await deriveAesKey(dh1, dh2, ephPkBytes, 'decrypt');
    const ad = new TextEncoder().encode(buildAd(senderId, recipientId, messageId, timestamp));

    try {
        const plaintext = await crypto.subtle.decrypt(
            { name: 'AES-GCM', iv: nonce, additionalData: ad },
            messageKey,
            ciphertext,
        );
        return new TextDecoder().decode(plaintext);
    } catch {
        throw new Error('Decryption failed: AD mismatch or key mismatch');
    }
}
