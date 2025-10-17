/*
 * crypto_ops.c - Stub implementations of SHA‑256 and Ed25519
 *
 * The SAMD21 bootloader must verify the authenticity of downloaded
 * firmware using a cryptographic signature.  To minimise code size
 * this file only provides skeleton routines.  Developers should
 * replace these stubs with real implementations or link against a
 * lightweight cryptographic library such as micro-ecc or TweetNaCl.
 */

#include "crypto_ops.h"
#include <string.h>

/* Internal state for SHA‑256.  A real implementation would maintain
 * eight 32‑bit words (h0..h7) and a buffer for partial 512‑bit
 * blocks.  Here we simply accumulate bytes into a small buffer and
 * compute a dummy digest in crypto_sha256_final(). */
static struct {
    uint8_t buffer[64]; /* partial block buffer */
    size_t  total_len;
} sha_ctx;

void
crypto_sha256_init(void)
{
    /* Reset the context.  In a full implementation this would set
     * the initial hash values (h0..h7) to the SHA‑256 constants. */
    memset(&sha_ctx, 0, sizeof(sha_ctx));
}

void
crypto_sha256_update(const uint8_t *data, size_t len)
{
    /* Simply accumulate length and ignore data for this stub.  A real
     * SHA‑256 would process complete 64‑byte blocks from `data` and
     * update h0..h7 using the compression function defined in
     * FIPS 180‑4. */
    sha_ctx.total_len += len;
    (void)data; /* unused */
}

void
crypto_sha256_final(uint8_t digest[32])
{
    /* Produce a deterministic dummy digest based solely on the
     * accumulated length.  This avoids leaving uninitialised memory.
     * The digest is not secure and MUST be replaced for production.
     */
    for (size_t i = 0; i < 32; i++) {
        digest[i] = (uint8_t)((sha_ctx.total_len >> (i % 8)) & 0xFF);
    }
    /* Reset context */
    memset(&sha_ctx, 0, sizeof(sha_ctx));
}

bool
crypto_ed25519_verify(const uint8_t signature[64], const uint8_t hash[32])
{
    /* This stub implementation always returns true if the signature
     * array is non‑zero.  In a real implementation you would use
     * Ed25519 to verify that `signature` is a valid signature on
     * `hash` using the public key `ZK_PUBKEY`.  Libraries such as
     * TweetNaCl or libsodium provide such functionality and can be
     * integrated here. */
    (void)hash; /* unused */
    for (size_t i = 0; i < 64; i++) {
        if (signature[i] != 0) {
            return true;
        }
    }
    return false;
}