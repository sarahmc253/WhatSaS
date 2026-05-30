import { generateKeypair } from './keypair.js';
import { importKeyMaterial, hkdfDerive } from './hkdf.js';
import { HKDF_INFO_MSG_ENC } from './constants.js';

// Two-DH DHKEM matching hpke_utils.hpp hpkeSend/hpkeReceive:
//   DH1 = X25519(eph_sk,    recipient_pk)  — confidentiality / forward secrecy
//   DH2 = X25519(sender_sk, recipient_pk)  — sender authentication
//   IKM = DH1 || DH2  (64 bytes)
//   PRK = HKDF-Extract(salt=eph_pk, IKM)
//   key = HKDF-Expand(PRK, info)

export async function encryptMessage(plaintext, recipientPublicKey, senderPrivateKey) {
    const { publicKey: ephemeralPublicKey, privateKey: ephemeralPrivateKey } = await generateKeypair();

    const dh1 = new Uint8Array(await crypto.subtle.deriveBits(
        { name: 'X25519', public: recipientPublicKey },
        ephemeralPrivateKey,
        256,
    ));
    const dh2 = new Uint8Array(await crypto.subtle.deriveBits(
        { name: 'X25519', public: recipientPublicKey },
        senderPrivateKey,
        256,
    ));

    const ikm = new Uint8Array(64);
    ikm.set(dh1, 0);
    ikm.set(dh2, 32);

    const ephPkBytes = new Uint8Array(await crypto.subtle.exportKey('raw', ephemeralPublicKey));

    const keyMaterial     = await importKeyMaterial(ikm);
    const messageKeyBytes = await hkdfDerive(keyMaterial, HKDF_INFO_MSG_ENC, ephPkBytes, 32);

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

export async function decryptMessage(ciphertext, nonce, senderEphemeralPublicKey, recipientPrivateKey, senderPublicKey) {
    try {
        const dh1 = new Uint8Array(await crypto.subtle.deriveBits(
            { name: 'X25519', public: senderEphemeralPublicKey },
            recipientPrivateKey,
            256,
        ));
        const dh2 = new Uint8Array(await crypto.subtle.deriveBits(
            { name: 'X25519', public: senderPublicKey },
            recipientPrivateKey,
            256,
        ));

        const ikm = new Uint8Array(64);
        ikm.set(dh1, 0);
        ikm.set(dh2, 32);

        const ephPkBytes = new Uint8Array(await crypto.subtle.exportKey('raw', senderEphemeralPublicKey));

        const keyMaterial     = await importKeyMaterial(ikm);
        const messageKeyBytes = await hkdfDerive(keyMaterial, HKDF_INFO_MSG_ENC, ephPkBytes, 32);
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
