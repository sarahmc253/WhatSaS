/**
 * Compute a Merkle root from an array of hex-encoded hashes.
 *
 * Mirrors _merkle_root() in server/app/messages/anchor.py exactly:
 *   - Leaves are the raw bytes of each hash (0x prefix stripped)
 *   - Odd layers duplicate the last node before pairing
 *   - Each pair is keccak256(left || right)
 *   - Returns a 0x-prefixed 32-byte hex root
 *
 * Requires ethers.js v6 UMD to be loaded as window.ethers.
 *
 * @param {string[]} hexHashes  Array of 0x-prefixed or bare hex hash strings
 * @returns {string}            0x-prefixed hex Merkle root
 */
export function merkleRoot(hexHashes) {
    const { keccak256, concat, getBytes } = window.ethers;

    // Normalise to 0x-prefixed so getBytes() accepts every element.
    // getBytes() decodes hex pairs to raw bytes — identical to Python bytes.fromhex().
    let layer = hexHashes.map(h => h.startsWith('0x') ? h : '0x' + h);

    while (layer.length > 1) {
        if (layer.length % 2 === 1) {
            layer.push(layer[layer.length - 1]);
        }
        const next = [];
        for (let i = 0; i < layer.length; i += 2) {
            // concat produces a 64-byte Uint8Array matching Python's layer[i] + layer[i+1].
            // keccak256 of that matches Web3.keccak(primitive=...) on the same bytes.
            next.push(keccak256(concat([getBytes(layer[i]), getBytes(layer[i + 1])])));
        }
        layer = next;
    }

    return layer[0];
}
