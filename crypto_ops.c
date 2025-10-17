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

/* Rotate right helper for 32-bit values */
#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 logical functions */
#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)        (ROTR32((x), 2) ^ ROTR32((x), 13) ^ ROTR32((x), 22))
#define EP1(x)        (ROTR32((x), 6) ^ ROTR32((x), 11) ^ ROTR32((x), 25))
#define SIG0(x)       (ROTR32((x), 7) ^ ROTR32((x), 18) ^ ((x) >> 3))
#define SIG1(x)       (ROTR32((x), 17) ^ ROTR32((x), 19) ^ ((x) >> 10))

/* Initial hash values (H0..H7) and round constants (K) from FIPS 180-4. */
static const uint32_t sha256_initial_state[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

static const uint32_t sha256_k[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFF9u, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

/* Internal state for SHA‑256 consisting of the eight working words,
 * a 64-byte buffer for partial blocks, and the total number of bytes
 * processed so far.  The state is kept static so the bootloader does
 * not require dynamic allocation. */
static struct {
    uint32_t h[8];
    uint8_t  buffer[64];
    size_t   buffer_len;
    uint64_t total_len;
} sha_ctx;

static void
crypto_sha256_process_block(const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (size_t i = 0; i < 16; i++) {
        size_t j = i * 4;
        w[i] = ((uint32_t)block[j] << 24) |
               ((uint32_t)block[j + 1] << 16) |
               ((uint32_t)block[j + 2] << 8) |
               ((uint32_t)block[j + 3]);
    }
    for (size_t i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    a = sha_ctx.h[0];
    b = sha_ctx.h[1];
    c = sha_ctx.h[2];
    d = sha_ctx.h[3];
    e = sha_ctx.h[4];
    f = sha_ctx.h[5];
    g = sha_ctx.h[6];
    h = sha_ctx.h[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t temp1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        uint32_t temp2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    sha_ctx.h[0] += a;
    sha_ctx.h[1] += b;
    sha_ctx.h[2] += c;
    sha_ctx.h[3] += d;
    sha_ctx.h[4] += e;
    sha_ctx.h[5] += f;
    sha_ctx.h[6] += g;
    sha_ctx.h[7] += h;
}

void
crypto_sha256_init(void)
{
    memcpy(sha_ctx.h, sha256_initial_state, sizeof(sha_ctx.h));
    sha_ctx.buffer_len = 0;
    sha_ctx.total_len = 0;
}

void
crypto_sha256_update(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    sha_ctx.total_len += len;

    size_t offset = 0;
    if (sha_ctx.buffer_len > 0) {
        size_t space = 64u - sha_ctx.buffer_len;
        size_t take = (len < space) ? len : space;
        memcpy(&sha_ctx.buffer[sha_ctx.buffer_len], &data[offset], take);
        sha_ctx.buffer_len += take;
        offset += take;
        len -= take;
        if (sha_ctx.buffer_len == 64u) {
            crypto_sha256_process_block(sha_ctx.buffer);
            sha_ctx.buffer_len = 0;
        }
    }

    while (len >= 64u) {
        crypto_sha256_process_block(&data[offset]);
        offset += 64u;
        len -= 64u;
    }

    if (len > 0) {
        memcpy(sha_ctx.buffer, &data[offset], len);
        sha_ctx.buffer_len = len;
    }
}

void
crypto_sha256_final(uint8_t digest[32])
{
    uint64_t bit_len = sha_ctx.total_len * 8u;
    size_t pad_index = sha_ctx.buffer_len;

    sha_ctx.buffer[pad_index++] = 0x80u;

    if (pad_index > 56u) {
        while (pad_index < 64u) {
            sha_ctx.buffer[pad_index++] = 0;
        }
        crypto_sha256_process_block(sha_ctx.buffer);
        pad_index = 0;
    }

    while (pad_index < 56u) {
        sha_ctx.buffer[pad_index++] = 0;
    }

    for (int i = 7; i >= 0; i--) {
        sha_ctx.buffer[pad_index++] = (uint8_t)((bit_len >> (i * 8)) & 0xFFu);
    }

    crypto_sha256_process_block(sha_ctx.buffer);

    for (size_t i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)(sha_ctx.h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(sha_ctx.h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(sha_ctx.h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(sha_ctx.h[i]);
    }

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