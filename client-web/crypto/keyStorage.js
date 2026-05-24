import { generateSalt, deriveLocalKEK } from './kek.js';
import { importKeyMaterial, hkdfDerive } from './hkdf.js';
import { HKDF_INFO_DEK_WRAP } from './constants.js';

function toB64(bytes) {
    // Array.from avoids the spread-operator stack-overflow on large Uint8Arrays
    return btoa(Array.from(bytes, b => String.fromCharCode(b)).join(''));
}

function fromB64(str) {
    return Uint8Array.from(atob(str), c => c.charCodeAt(0));
}

async function deriveWrapKey(passphrase, salt) {
    const kekRaw      = await deriveLocalKEK(passphrase, salt);
    const kekMaterial = await importKeyMaterial(kekRaw);
    const wrapBytes   = await hkdfDerive(kekMaterial, HKDF_INFO_DEK_WRAP, salt, 32);
    return crypto.subtle.importKey(
        'raw', wrapBytes, { name: 'AES-GCM' }, false, ['wrapKey', 'unwrapKey'],
    );
}

export async function encryptPrivateKey(privateKeyBytes, passphrase) {
    const salt    = generateSalt();
    const wrapKey = await deriveWrapKey(passphrase, salt);

    const dekRaw = crypto.getRandomValues(new Uint8Array(32));
    const dekKey = await crypto.subtle.importKey(
        'raw', dekRaw, { name: 'AES-GCM' }, true, ['encrypt', 'decrypt'],
    );

    const kekNonce   = crypto.getRandomValues(new Uint8Array(12));
    const wrappedDek = new Uint8Array(await crypto.subtle.wrapKey(
        'raw', dekKey, wrapKey, { name: 'AES-GCM', iv: kekNonce },
    ));

    const dekNonce   = crypto.getRandomValues(new Uint8Array(12));
    const ciphertext = new Uint8Array(await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv: dekNonce },
        dekKey,
        privateKeyBytes,
    ));

    return new EncryptedPrivateKey(salt, kekNonce, wrappedDek, dekNonce, ciphertext);
}

export async function decryptPrivateKey(encrypted, passphrase) {
    let wrapKey, dekKey;
    try {
        wrapKey = await deriveWrapKey(passphrase, encrypted.salt);
        dekKey  = await crypto.subtle.unwrapKey(
            'raw',
            encrypted.wrappedDek,
            wrapKey,
            { name: 'AES-GCM', iv: encrypted.kekNonce },
            { name: 'AES-GCM' },
            false,
            ['decrypt'],
        );
    } catch {
        throw new Error('Decryption failed: wrong passphrase or corrupted key material');
    }

    try {
        const plaintext = await crypto.subtle.decrypt(
            { name: 'AES-GCM', iv: encrypted.dekNonce },
            dekKey,
            encrypted.ciphertext,
        );
        return new Uint8Array(plaintext);
    } catch {
        throw new Error('Decryption failed: wrong passphrase or corrupted key material');
    }
}

export class EncryptedPrivateKey {
    /**
     * @param {Uint8Array} salt
     * @param {Uint8Array} kekNonce
     * @param {Uint8Array} wrappedDek
     * @param {Uint8Array} dekNonce
     * @param {Uint8Array} ciphertext
     */
    constructor(salt, kekNonce, wrappedDek, dekNonce, ciphertext) {
        this.salt        = salt;
        this.kekNonce    = kekNonce;
        this.wrappedDek  = wrappedDek;
        this.dekNonce    = dekNonce;
        this.ciphertext  = ciphertext;
    }

    toJSON() {
        return {
            salt:        toB64(this.salt),
            kek_nonce:   toB64(this.kekNonce),
            wrapped_dek: toB64(this.wrappedDek),
            dek_nonce:   toB64(this.dekNonce),
            ciphertext:  toB64(this.ciphertext),
        };
    }

    static fromJSON(obj) {
        const fields = ['salt', 'kek_nonce', 'wrapped_dek', 'dek_nonce', 'ciphertext'];
        for (const field of fields) {
            if (typeof obj?.[field] !== 'string') {
                throw new Error(`EncryptedPrivateKey.fromJSON: missing or invalid field "${field}"`);
            }
        }

        const salt       = fromB64(obj.salt);
        const kekNonce   = fromB64(obj.kek_nonce);
        const wrappedDek = fromB64(obj.wrapped_dek);
        const dekNonce   = fromB64(obj.dek_nonce);
        const ciphertext = fromB64(obj.ciphertext);

        if (salt.byteLength !== 16)
            throw new Error(`EncryptedPrivateKey.fromJSON: salt must be 16 bytes, got ${salt.byteLength}`);
        if (kekNonce.byteLength !== 12)
            throw new Error(`EncryptedPrivateKey.fromJSON: kek_nonce must be 12 bytes, got ${kekNonce.byteLength}`);
        if (dekNonce.byteLength !== 12)
            throw new Error(`EncryptedPrivateKey.fromJSON: dek_nonce must be 12 bytes, got ${dekNonce.byteLength}`);
        // 32-byte DEK + 16-byte AES-GCM tag
        if (wrappedDek.byteLength < 48)
            throw new Error(`EncryptedPrivateKey.fromJSON: wrapped_dek too short (${wrappedDek.byteLength} bytes)`);
        // 32-byte X25519 private key + 16-byte AES-GCM tag
        if (ciphertext.byteLength < 48)
            throw new Error(`EncryptedPrivateKey.fromJSON: ciphertext too short (${ciphertext.byteLength} bytes)`);

        return new EncryptedPrivateKey(salt, kekNonce, wrappedDek, dekNonce, ciphertext);
    }
}
