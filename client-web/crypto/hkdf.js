const encoder = new TextEncoder();

export async function hkdfDerive(keyMaterial, info, salt, lengthBytes) {
    const bits = await crypto.subtle.deriveBits(
        {
            name: 'HKDF',
            hash: 'SHA-256',
            info: encoder.encode(info),
            salt,
        },
        keyMaterial,
        lengthBytes * 8,
    );
    return new Uint8Array(bits);
}

export async function importKeyMaterial(rawBytes) {
    return crypto.subtle.importKey(
        'raw',
        rawBytes,
        { name: 'HKDF' },
        false,          // not extractable — key material never leaves the Web Crypto API
        ['deriveKey', 'deriveBits'],
    );
}
