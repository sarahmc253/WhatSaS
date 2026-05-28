export function generateSalt() {
    return crypto.getRandomValues(new Uint8Array(16));
}

export async function deriveLocalKEK(passphrase, salt) {
    // Params match crypto-library/key_derivation.py derive_local_kek exactly
    // so key material derived in Python and in the browser is interoperable.
    const result = await argon2.hash({
        pass:        passphrase,
        salt,
        type:        argon2.ArgonType.Argon2id,
        mem:         32768,   // KiB — 32 MB
        time:        2,
        parallelism: 4,
        hashLen:     32,
        distPath:    null,    // run on main thread — avoids blob: worker blocked by MetaMask CSP
    });

    return result.hash; // raw Uint8Array — callers import into Web Crypto as needed
}
