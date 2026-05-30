export async function getPublicKeyBytes(publicKey) {
    return new Uint8Array(await crypto.subtle.exportKey('raw', publicKey));
}

export async function getPrivateKeyBytes(privateKey) {
    // Never log or transmit these bytes — pass them only to encryptPrivateKey for local storage
    return new Uint8Array(await crypto.subtle.exportKey('pkcs8', privateKey));
}

export async function getPublicKeyB64(publicKey) {
    const bytes = await getPublicKeyBytes(publicKey);
    return btoa(Array.from(bytes, b => String.fromCharCode(b)).join(''));
}

export async function generateKeypair() {
    const { publicKey, privateKey } = await crypto.subtle.generateKey(
        { name: 'X25519' },
        true,               // extractable — private key exported as pkcs8 for storage; public key as raw for publishing
        ['deriveKey', 'deriveBits'],
    );
    return { publicKey, privateKey };
}
