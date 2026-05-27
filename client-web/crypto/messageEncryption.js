import { generateKeypair } from './keypair.js';
import { importKeyMaterial, hkdfDerive } from './hkdf.js';
import { HKDF_INFO_MSG_ENC } from './constants.js';

export async function encryptMessage(plaintext, recipientPublicKey) {
    const { publicKey: ephemeralPublicKey, privateKey: ephemeralPrivateKey } = await generateKeypair();

    const sharedSecret = new Uint8Array(await crypto.subtle.deriveBits(
        { name: 'X25519', public: recipientPublicKey },
        ephemeralPrivateKey,
        256, // X25519 always produces 32 bytes
    ));

    const keyMaterial  = await importKeyMaterial(sharedSecret);
    const messageKeyBytes = await hkdfDerive(keyMaterial, HKDF_INFO_MSG_ENC, new Uint8Array(0), 32);

    const messageKey = await crypto.subtle.importKey(
        'raw', messageKeyBytes, { name: 'AES-GCM' }, false, ['encrypt'],
    );

    const nonce      = crypto.getRandomValues(new Uint8Array(12));
    const ciphertext = new Uint8Array(await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv: nonce },
        messageKey,
        new TextEncoder().encode(plaintext),
    ));

    return { ephemeralPublicKey, nonce, ciphertext };
}

export async function decryptMessage(ciphertext, nonce, senderEphemeralPublicKey, recipientPrivateKey) {
    try {
        const sharedSecret = new Uint8Array(await crypto.subtle.deriveBits(
            { name: 'X25519', public: senderEphemeralPublicKey },
            recipientPrivateKey,
            256,
        ));

        const keyMaterial     = await importKeyMaterial(sharedSecret);
        const messageKeyBytes = await hkdfDerive(keyMaterial, HKDF_INFO_MSG_ENC, new Uint8Array(0), 32);
        const messageKey      = await crypto.subtle.importKey(
            'raw', messageKeyBytes, { name: 'AES-GCM' }, false, ['decrypt'],
        );

        const plaintext = await crypto.subtle.decrypt(
            { name: 'AES-GCM', iv: nonce },
            messageKey,
            ciphertext,
        );

        return new TextDecoder().decode(plaintext);
    } catch {
        throw new Error('Decryption failed: message authentication failed or key mismatch');
    }
}
