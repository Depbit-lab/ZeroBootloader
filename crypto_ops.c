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

/* -------------------------------------------------------------------------
 * 128-bit helper utilities
 * -------------------------------------------------------------------------
 *
 * The Ed25519 field arithmetic below uses 128-bit intermediates when
 * available.  Some bare-metal toolchains (notably arm-none-eabi-gcc for
 * Cortex-M0/M0+) do not provide the non-standard __uint128_t type, so we
 * fall back to a tiny struct-based implementation when required.  The
 * helper routines expose a minimal interface that mirrors the operations
 * required by the finite-field routines (addition, multiplication, and
 * small shifts), keeping the call sites readable while remaining fully
 * portable.
 */
#if defined(__SIZEOF_INT128__)
typedef __uint128_t fe_u128;

static inline fe_u128
fe_u128_add(fe_u128 a, fe_u128 b)
{
    return a + b;
}

static inline fe_u128
fe_u128_add_u64(fe_u128 a, uint64_t b)
{
    return a + (fe_u128)b;
}

static inline fe_u128
fe_u128_mul64(uint64_t a, uint64_t b)
{
    return (fe_u128)a * (fe_u128)b;
}

static inline uint64_t
fe_u128_lo(fe_u128 a)
{
    return (uint64_t)a;
}

static inline uint64_t
fe_u128_shr_u64(fe_u128 a, unsigned shift)
{
    return (uint64_t)(a >> shift);
}
#else
typedef struct {
    uint64_t lo;
    uint64_t hi;
} fe_u128;

static inline fe_u128
fe_u128_add(fe_u128 a, fe_u128 b)
{
    fe_u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}

static inline fe_u128
fe_u128_add_u64(fe_u128 a, uint64_t b)
{
    fe_u128 r;
    r.lo = a.lo + b;
    r.hi = a.hi + (r.lo < a.lo);
    return r;
}

static inline fe_u128
fe_u128_mul64(uint64_t a, uint64_t b)
{
    uint64_t alo = (uint32_t)a;
    uint64_t ahi = a >> 32;
    uint64_t blo = (uint32_t)b;
    uint64_t bhi = b >> 32;

    uint64_t lo = alo * blo;
    uint64_t mid1 = alo * bhi;
    uint64_t mid2 = ahi * blo;
    uint64_t hi = ahi * bhi;

    uint64_t mid = mid1 + mid2;
    uint64_t mid_carry = (mid < mid1) ? 1ULL : 0ULL;
    uint64_t lo_res = lo + (mid << 32);
    uint64_t carry = (lo_res < lo) ? 1ULL : 0ULL;

    fe_u128 r;
    r.lo = lo_res;
    r.hi = hi + (mid >> 32) + (mid_carry << 32) + carry;
    return r;
}

static inline uint64_t
fe_u128_lo(fe_u128 a)
{
    return a.lo;
}

static inline uint64_t
fe_u128_shr_u64(fe_u128 a, unsigned shift)
{
    if (shift == 0) {
        return a.lo;
    } else if (shift < 64u) {
        return (a.lo >> shift) | (a.hi << (64u - shift));
    } else if (shift < 128u) {
        return a.hi >> (shift - 64u);
    }
    return 0;
}
#endif

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

/* -------------------------------------------------------------------------
 * Ed25519 signature verification
 * -------------------------------------------------------------------------
 *
 * The implementation below is a compact, self-contained Ed25519
 * verifier suitable for bare-metal use.  It is based on the formulas
 * from the original Ed25519 paper and the public-domain ref10 code but
 * rewritten to avoid large pre-computed tables.  Field arithmetic is
 * carried out using a 5-limb (51-bit) representation which maps well to
 * 64-bit intermediate values available on the host system.  The code is
 * optimised for clarity and small footprint rather than throughput, but
 * it is more than adequate for a bootloader that verifies a single
 * signature at boot.
 */

#include <stddef.h>

/* ---- SHA-512 ---------------------------------------------------------- */

typedef struct {
    uint64_t state[8];
    uint64_t total_len[2];
    uint8_t buffer[128];
    size_t buffer_len;
} sha512_ctx_t;

static const uint64_t sha512_initial_state[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
    0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
    0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
    0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
    0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static inline uint64_t
rotr64(uint64_t x, uint8_t n)
{
    return (x >> n) | (x << (64u - n));
}

static void
sha512_process_block(sha512_ctx_t *ctx, const uint8_t block[128])
{
    uint64_t w[80];

    for (size_t i = 0; i < 16; i++) {
        size_t j = i * 8u;
        w[i] = ((uint64_t)block[j] << 56) |
               ((uint64_t)block[j + 1] << 48) |
               ((uint64_t)block[j + 2] << 40) |
               ((uint64_t)block[j + 3] << 32) |
               ((uint64_t)block[j + 4] << 24) |
               ((uint64_t)block[j + 5] << 16) |
               ((uint64_t)block[j + 6] << 8) |
               ((uint64_t)block[j + 7]);
    }
    for (size_t i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint64_t a = ctx->state[0];
    uint64_t b = ctx->state[1];
    uint64_t c = ctx->state[2];
    uint64_t d = ctx->state[3];
    uint64_t e = ctx->state[4];
    uint64_t f = ctx->state[5];
    uint64_t g = ctx->state[6];
    uint64_t h = ctx->state[7];

    for (size_t i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t temp1 = h + S1 + ch + sha512_k[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void
sha512_init(sha512_ctx_t *ctx)
{
    memcpy(ctx->state, sha512_initial_state, sizeof(ctx->state));
    ctx->total_len[0] = 0;
    ctx->total_len[1] = 0;
    ctx->buffer_len = 0;
}

static void
sha512_update(sha512_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }

    uint64_t low = ctx->total_len[1] + (uint64_t)len;
    if (low < ctx->total_len[1]) {
        ctx->total_len[0]++;
    }
    ctx->total_len[1] = low;

    size_t offset = 0;
    if (ctx->buffer_len > 0) {
        size_t space = 128u - ctx->buffer_len;
        size_t take = (len < space) ? len : space;
        memcpy(&ctx->buffer[ctx->buffer_len], &data[offset], take);
        ctx->buffer_len += take;
        offset += take;
        len -= take;
        if (ctx->buffer_len == 128u) {
            sha512_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }

    while (len >= 128u) {
        sha512_process_block(ctx, &data[offset]);
        offset += 128u;
        len -= 128u;
    }

    if (len > 0) {
        memcpy(ctx->buffer, &data[offset], len);
        ctx->buffer_len = len;
    }
}

static void
sha512_final(sha512_ctx_t *ctx, uint8_t out[64])
{
    uint64_t bit_hi = (ctx->total_len[0] << 3) | (ctx->total_len[1] >> 61);
    uint64_t bit_lo = ctx->total_len[1] << 3;

    ctx->buffer[ctx->buffer_len++] = 0x80u;
    if (ctx->buffer_len > 112u) {
        while (ctx->buffer_len < 128u) {
            ctx->buffer[ctx->buffer_len++] = 0;
        }
        sha512_process_block(ctx, ctx->buffer);
        ctx->buffer_len = 0;
    }

    while (ctx->buffer_len < 112u) {
        ctx->buffer[ctx->buffer_len++] = 0;
    }

    for (int i = 7; i >= 0; i--) {
        ctx->buffer[ctx->buffer_len++] = (uint8_t)(bit_hi >> (i * 8));
    }
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[ctx->buffer_len++] = (uint8_t)(bit_lo >> (i * 8));
    }

    sha512_process_block(ctx, ctx->buffer);

    for (size_t i = 0; i < 8; i++) {
        out[i * 8 + 0] = (uint8_t)(ctx->state[i] >> 56);
        out[i * 8 + 1] = (uint8_t)(ctx->state[i] >> 48);
        out[i * 8 + 2] = (uint8_t)(ctx->state[i] >> 40);
        out[i * 8 + 3] = (uint8_t)(ctx->state[i] >> 32);
        out[i * 8 + 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 8 + 5] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 8 + 6] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 8 + 7] = (uint8_t)(ctx->state[i]);
    }

    memset(ctx, 0, sizeof(*ctx));
}

static void
sha512_three(const uint8_t *a, size_t alen,
             const uint8_t *b, size_t blen,
             const uint8_t *c, size_t clen,
             uint8_t out[64])
{
    sha512_ctx_t ctx;
    sha512_init(&ctx);
    if (a && alen) {
        sha512_update(&ctx, a, alen);
    }
    if (b && blen) {
        sha512_update(&ctx, b, blen);
    }
    if (c && clen) {
        sha512_update(&ctx, c, clen);
    }
    sha512_final(&ctx, out);
}

/* ---- Helper ----------------------------------------------------------- */

static int
crypto_verify_32(const uint8_t *a, const uint8_t *b)
{
    uint32_t diff = 0;
    for (size_t i = 0; i < 32; i++) {
        diff |= (uint32_t)(a[i] ^ b[i]);
    }
    return (int)((1 & ((diff - 1) >> 8)) - 1);
}

/* ---- Field arithmetic mod 2^255-19 ---------------------------------- */

typedef struct {
    uint64_t v[5];
} fe51;

#define FE51_MASK ((uint64_t)((1ULL << 51) - 1ULL))

static void fe51_setzero(fe51 *r) { memset(r, 0, sizeof(*r)); }
static void fe51_setone(fe51 *r)  { fe51_setzero(r); r->v[0] = 1; }
static void fe51_copy(fe51 *r, const fe51 *a) { memcpy(r->v, a->v, sizeof(r->v)); }

static uint64_t
load64_le(const uint8_t *in)
{
    return ((uint64_t)in[0])        | ((uint64_t)in[1] << 8) |
           ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24) |
           ((uint64_t)in[4] << 32) | ((uint64_t)in[5] << 40) |
           ((uint64_t)in[6] << 48) | ((uint64_t)in[7] << 56);
}

static void
fe51_reduce(fe51 *r)
{
    uint64_t c0 = r->v[0] >> 51; r->v[0] &= FE51_MASK; r->v[1] += c0;
    uint64_t c1 = r->v[1] >> 51; r->v[1] &= FE51_MASK; r->v[2] += c1;
    uint64_t c2 = r->v[2] >> 51; r->v[2] &= FE51_MASK; r->v[3] += c2;
    uint64_t c3 = r->v[3] >> 51; r->v[3] &= FE51_MASK; r->v[4] += c3;
    uint64_t c4 = r->v[4] >> 51; r->v[4] &= FE51_MASK; r->v[0] += c4 * 19ULL;
    c0 = r->v[0] >> 51; r->v[0] &= FE51_MASK; r->v[1] += c0;
    r->v[1] &= FE51_MASK;
}

static void
fe51_frombytes(fe51 *r, const uint8_t s[32])
{
    fe51_setzero(r);
    r->v[0] = load64_le(s) & FE51_MASK;
    r->v[1] = (load64_le(s + 6) >> 3) & FE51_MASK;
    r->v[2] = (load64_le(s + 12) >> 6) & FE51_MASK;
    r->v[3] = (load64_le(s + 19) >> 1) & FE51_MASK;
    r->v[4] = (load64_le(s + 24) >> 12) & FE51_MASK;
}

static void
store64_le(uint8_t out[8], uint64_t value)
{
    out[0] = (uint8_t)(value);
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
    out[4] = (uint8_t)(value >> 32);
    out[5] = (uint8_t)(value >> 40);
    out[6] = (uint8_t)(value >> 48);
    out[7] = (uint8_t)(value >> 56);
}

static void
fe51_tobytes(uint8_t s[32], fe51 *f)
{
    fe51_reduce(f);
    uint64_t t0 = f->v[0] | (f->v[1] << 51);
    uint64_t t1 = (f->v[1] >> 13) | (f->v[2] << 38);
    uint64_t t2 = (f->v[2] >> 26) | (f->v[3] << 25);
    uint64_t t3 = (f->v[3] >> 39) | (f->v[4] << 12);

    store64_le(&s[0], t0);
    store64_le(&s[8], t1);
    store64_le(&s[16], t2);
    store64_le(&s[24], t3);
}

static void fe51_add(fe51 *r, const fe51 *a, const fe51 *b)
{
    for (int i = 0; i < 5; i++) {
        r->v[i] = a->v[i] + b->v[i];
    }
}

static const uint64_t FE51_2P[5] = {
    4503599627370458ULL, 4503599627370494ULL, 4503599627370494ULL,
    4503599627370494ULL, 4503599627370494ULL
};

static void fe51_sub(fe51 *r, const fe51 *a, const fe51 *b)
{
    for (int i = 0; i < 5; i++) {
        r->v[i] = a->v[i] + FE51_2P[i] - b->v[i];
    }
    fe51_reduce(r);
}

static void fe51_neg(fe51 *r, const fe51 *a)
{
    for (int i = 0; i < 5; i++) {
        r->v[i] = FE51_2P[i] - a->v[i];
    }
    fe51_reduce(r);
}

static void fe51_cmov(fe51 *r, const fe51 *a, int flag)
{
    uint64_t mask = (uint64_t)(-flag);
    for (int i = 0; i < 5; i++) {
        uint64_t diff = r->v[i] ^ a->v[i];
        diff &= mask;
        r->v[i] ^= diff;
    }
}

static void fe51_mul(fe51 *r, const fe51 *a, const fe51 *b)
{
    uint64_t a1_19 = a->v[1] * 19ULL;
    uint64_t a2_19 = a->v[2] * 19ULL;
    uint64_t a3_19 = a->v[3] * 19ULL;
    uint64_t a4_19 = a->v[4] * 19ULL;

    fe_u128 t0 = fe_u128_mul64(a->v[0], b->v[0]);
    t0 = fe_u128_add(t0, fe_u128_mul64(a1_19, b->v[4]));
    t0 = fe_u128_add(t0, fe_u128_mul64(a2_19, b->v[3]));
    t0 = fe_u128_add(t0, fe_u128_mul64(a3_19, b->v[2]));
    t0 = fe_u128_add(t0, fe_u128_mul64(a4_19, b->v[1]));

    fe_u128 t1 = fe_u128_mul64(a->v[0], b->v[1]);
    t1 = fe_u128_add(t1, fe_u128_mul64(a->v[1], b->v[0]));
    t1 = fe_u128_add(t1, fe_u128_mul64(a2_19, b->v[4]));
    t1 = fe_u128_add(t1, fe_u128_mul64(a3_19, b->v[3]));
    t1 = fe_u128_add(t1, fe_u128_mul64(a4_19, b->v[2]));

    fe_u128 t2 = fe_u128_mul64(a->v[0], b->v[2]);
    t2 = fe_u128_add(t2, fe_u128_mul64(a->v[1], b->v[1]));
    t2 = fe_u128_add(t2, fe_u128_mul64(a->v[2], b->v[0]));
    t2 = fe_u128_add(t2, fe_u128_mul64(a3_19, b->v[4]));
    t2 = fe_u128_add(t2, fe_u128_mul64(a4_19, b->v[3]));

    fe_u128 t3 = fe_u128_mul64(a->v[0], b->v[3]);
    t3 = fe_u128_add(t3, fe_u128_mul64(a->v[1], b->v[2]));
    t3 = fe_u128_add(t3, fe_u128_mul64(a->v[2], b->v[1]));
    t3 = fe_u128_add(t3, fe_u128_mul64(a->v[3], b->v[0]));
    t3 = fe_u128_add(t3, fe_u128_mul64(a4_19, b->v[4]));

    fe_u128 t4 = fe_u128_mul64(a->v[0], b->v[4]);
    t4 = fe_u128_add(t4, fe_u128_mul64(a->v[1], b->v[3]));
    t4 = fe_u128_add(t4, fe_u128_mul64(a->v[2], b->v[2]));
    t4 = fe_u128_add(t4, fe_u128_mul64(a->v[3], b->v[1]));
    t4 = fe_u128_add(t4, fe_u128_mul64(a->v[4], b->v[0]));

    r->v[0] = fe_u128_lo(t0) & FE51_MASK; t1 = fe_u128_add_u64(t1, fe_u128_shr_u64(t0, 51));
    r->v[1] = fe_u128_lo(t1) & FE51_MASK; t2 = fe_u128_add_u64(t2, fe_u128_shr_u64(t1, 51));
    r->v[2] = fe_u128_lo(t2) & FE51_MASK; t3 = fe_u128_add_u64(t3, fe_u128_shr_u64(t2, 51));
    r->v[3] = fe_u128_lo(t3) & FE51_MASK; t4 = fe_u128_add_u64(t4, fe_u128_shr_u64(t3, 51));
    r->v[4] = fe_u128_lo(t4) & FE51_MASK;
    uint64_t carry = fe_u128_shr_u64(t4, 51);
    r->v[0] += carry * 19ULL;
    fe51_reduce(r);
}

static void fe51_sq(fe51 *r, const fe51 *a)
{
    fe51_mul(r, a, a);
}

static void fe51_pow22523(fe51 *r, const fe51 *z)
{
    fe51 t0, t1, t2;
    fe51_sq(&t0, z);          /* 2 */
    fe51_sq(&t1, &t0);        /* 4 */
    fe51_sq(&t1, &t1);        /* 8 */
    fe51_mul(&t1, z, &t1);    /* 9 */
    fe51_mul(&t0, &t0, &t1);  /* 11 */
    fe51_sq(&t2, &t0);        /* 22 */
    fe51_mul(&t1, &t1, &t2);  /* 31 */
    fe51_sq(&t2, &t1);        /* 62 */
    for (int i = 1; i < 5; i++) fe51_sq(&t2, &t2);
    fe51_mul(&t1, &t2, &t1);  /* 2^5 - 1 */
    fe51_sq(&t2, &t1);
    for (int i = 1; i < 10; i++) fe51_sq(&t2, &t2);
    fe51_mul(&t2, &t2, &t1);  /* 2^10 - 1 */
    fe51_sq(&t0, &t2);
    for (int i = 1; i < 20; i++) fe51_sq(&t0, &t0);
    fe51_mul(&t2, &t0, &t2);  /* 2^20 - 1 */
    fe51_sq(&t2, &t2);
    for (int i = 1; i < 10; i++) fe51_sq(&t2, &t2);
    fe51_mul(&t1, &t2, &t1);  /* 2^30 - 1 */
    fe51_sq(&t2, &t1);
    for (int i = 1; i < 50; i++) fe51_sq(&t2, &t2);
    fe51_mul(&t2, &t2, &t1);  /* 2^80 - 1 */
    fe51_sq(&t0, &t2);
    for (int i = 1; i < 100; i++) fe51_sq(&t0, &t0);
    fe51_mul(&t2, &t0, &t2);  /* 2^180 - 1 */
    fe51_sq(&t2, &t2);
    for (int i = 1; i < 50; i++) fe51_sq(&t2, &t2);
    fe51_mul(&t1, &t2, &t1);  /* 2^230 - 1 */
    fe51_sq(&t1, &t1);
    fe51_sq(&t1, &t1);
    fe51_mul(r, &t1, &t0);    /* 2^252 - 3 */
}

static void fe51_invert(fe51 *r, const fe51 *z)
{
    fe51 t0, t1;
    fe51_pow22523(&t0, z);
    fe51_sq(&t1, &t0);
    fe51_sq(&t1, &t1);
    fe51_mul(r, &t1, z);
}

static int fe51_is_negative(fe51 *a)
{
    fe51_reduce(a);
    return (int)(a->v[0] & 1ULL);
}

static int fe51_is_nonzero(const fe51 *a)
{
    fe51 t;
    fe51_copy(&t, a);
    fe51_reduce(&t);
    return (int)(t.v[0] | t.v[1] | t.v[2] | t.v[3] | t.v[4]);
}

static const fe51 FE51_CONST_ONE = {{1, 0, 0, 0, 0}};
static const fe51 EDWARDS_D = {{
    929955233495203ULL, 466365720129213ULL, 1662059464998953ULL,
    2033849074728123ULL, 1442794654840575ULL
}};
static const fe51 SQRT_M1 = {{
    1718705420411056ULL, 234908883556509ULL, 2233514472574048ULL,
    2117202627021982ULL, 765476049583133ULL
}};
static const fe51 BASEPOINT_X = {{
    1738742601995546ULL, 1146398526822698ULL, 2070867633025821ULL,
    562264141797630ULL, 587772402128613ULL
}};
static const fe51 BASEPOINT_Y = {{
    1801439850948184ULL, 1351079888211148ULL, 450359962737049ULL,
    900719925474099ULL, 1801439850948198ULL
}};

static const uint8_t ED25519_BASEPOINT[32] = {
    0x1a, 0xd5, 0x25, 0x8f, 0x60, 0x2d, 0x56, 0xc9,
    0xb2, 0xa7, 0x25, 0x95, 0x60, 0xc7, 0x2c, 0x69,
    0x5c, 0xdc, 0xd6, 0xfd, 0x31, 0xe2, 0xa4, 0xc0,
    0xfe, 0x53, 0x6e, 0xcd, 0xd3, 0x36, 0x69, 0x21
};

typedef struct {
    fe51 X;
    fe51 Y;
    fe51 Z;
    fe51 T;
} ge_p3;

static void ge_identity(ge_p3 *p)
{
    fe51_setzero(&p->X);
    fe51_setone(&p->Y);
    fe51_setone(&p->Z);
    fe51_setzero(&p->T);
}

static void ge_add(ge_p3 *r, const ge_p3 *p, const ge_p3 *q)
{
    fe51 Y1plusX1, Y1minusX1, Y2plusX2, Y2minusX2;
    fe51 A, B, C, D, E, F, G, H;
    ge_p3 tmp;

    fe51_add(&Y1plusX1, &p->Y, &p->X);
    fe51_sub(&Y1minusX1, &p->Y, &p->X);
    fe51_add(&Y2plusX2, &q->Y, &q->X);
    fe51_sub(&Y2minusX2, &q->Y, &q->X);

    fe51_mul(&A, &Y1minusX1, &Y2plusX2);
    fe51_mul(&B, &Y1plusX1, &Y2minusX2);
    fe51_mul(&C, &p->T, &q->T);
    fe51_mul(&C, &C, &EDWARDS_D);
    fe51_add(&C, &C, &C);
    fe51_mul(&D, &p->Z, &q->Z);
    fe51_add(&D, &D, &D);

    fe51_sub(&E, &B, &A);
    fe51_sub(&F, &D, &C);
    fe51_add(&G, &D, &C);
    fe51_add(&H, &B, &A);

    fe51_mul(&tmp.X, &E, &F);
    fe51_mul(&tmp.Y, &G, &H);
    fe51_mul(&tmp.Z, &F, &G);
    fe51_mul(&tmp.T, &E, &H);

    *r = tmp;
}

static void ge_double(ge_p3 *r, const ge_p3 *p)
{
    fe51 A, B, C, D, E, F, G, H;
    ge_p3 tmp;

    fe51_sq(&A, &p->X);
    fe51_sq(&B, &p->Y);
    fe51_sq(&C, &p->Z);
    fe51_add(&C, &C, &C);
    fe51_neg(&D, &A);
    fe51_add(&E, &p->X, &p->Y);
    fe51_sq(&E, &E);
    fe51_sub(&E, &E, &A);
    fe51_sub(&E, &E, &B);
    fe51_add(&G, &D, &B);
    fe51_sub(&F, &G, &C);
    fe51_sub(&H, &D, &B);

    fe51_mul(&tmp.X, &E, &F);
    fe51_mul(&tmp.Y, &G, &H);
    fe51_mul(&tmp.Z, &F, &G);
    fe51_mul(&tmp.T, &E, &H);

    *r = tmp;
}

static int ge_frombytes(ge_p3 *p, const uint8_t s[32])
{
    uint8_t buf[32];
    memcpy(buf, s, sizeof(buf));
    uint8_t sign = buf[31] >> 7;
    buf[31] &= 0x7fu;

    fe51_frombytes(&p->Y, buf);
    fe51_setone(&p->Z);

    fe51 y_sq, u, v;
    fe51_sq(&y_sq, &p->Y);
    fe51_sub(&u, &y_sq, &FE51_CONST_ONE);
    fe51_mul(&v, &y_sq, &EDWARDS_D);
    fe51_add(&v, &v, &FE51_CONST_ONE);

    fe51 v_sq, v_cube, x;
    fe51_sq(&v_sq, &v);
    fe51_mul(&v_cube, &v_sq, &v);
    fe51_mul(&x, &v_cube, &u);
    fe51_pow22523(&x, &x);
    fe51_mul(&x, &x, &v_cube);
    fe51_mul(&x, &x, &u);

    fe51 x_sq, vx_sq;
    fe51_sq(&x_sq, &x);
    fe51_mul(&vx_sq, &x_sq, &v);
    fe51_sub(&vx_sq, &vx_sq, &u);
    if (fe51_is_nonzero(&vx_sq)) {
        fe51_mul(&x, &x, &SQRT_M1);
        fe51_sq(&x_sq, &x);
        fe51_mul(&vx_sq, &x_sq, &v);
        fe51_sub(&vx_sq, &vx_sq, &u);
        if (fe51_is_nonzero(&vx_sq)) {
            return -1;
        }
    }

    fe51_reduce(&x);
    if (fe51_is_negative(&x) != (int)sign) {
        fe51_neg(&x, &x);
    }

    fe51_copy(&p->X, &x);
    fe51_mul(&p->T, &p->X, &p->Y);
    return 0;
}

static void ge_tobytes(uint8_t s[32], const ge_p3 *p)
{
    fe51 z_inv, x, y;
    fe51_invert(&z_inv, &p->Z);
    fe51_mul(&x, &p->X, &z_inv);
    fe51_mul(&y, &p->Y, &z_inv);
    fe51_reduce(&x);
    fe51_reduce(&y);

    fe51 y_copy = y;
    fe51_tobytes(s, &y_copy);
    fe51 x_copy = x;
    uint8_t x_bytes[32];
    fe51_tobytes(x_bytes, &x_copy);
    s[31] ^= (uint8_t)((x_bytes[0] & 1u) << 7);
}

static void ge_scalarmult(ge_p3 *r, const ge_p3 *p, const uint8_t scalar[32])
{
    ge_p3 result;
    ge_identity(&result);

    for (int i = 255; i >= 0; i--) {
        ge_double(&result, &result);
        if ((scalar[i >> 3] >> (i & 7)) & 1u) {
            ge_p3 tmp;
            ge_add(&tmp, &result, p);
            result = tmp;
        }
    }

    *r = result;
}

/* ---- Scalar arithmetic ----------------------------------------------- */

static uint64_t load_3(const uint8_t *in)
{
    return (uint64_t)in[0] | ((uint64_t)in[1] << 8) | ((uint64_t)in[2] << 16);
}

static uint64_t load_4(const uint8_t *in)
{
    return (uint64_t)in[0] | ((uint64_t)in[1] << 8) | ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24);
}

static void sc_reduce(uint8_t s[64])
{
    int64_t s0 = 2097151 & load_3(s);
    int64_t s1 = 2097151 & (load_4(s + 2) >> 5);
    int64_t s2 = 2097151 & (load_3(s + 5) >> 2);
    int64_t s3 = 2097151 & (load_4(s + 7) >> 7);
    int64_t s4 = 2097151 & (load_4(s + 10) >> 4);
    int64_t s5 = 2097151 & (load_3(s + 13) >> 1);
    int64_t s6 = 2097151 & (load_4(s + 15) >> 6);
    int64_t s7 = 2097151 & (load_3(s + 18) >> 3);
    int64_t s8 = 2097151 & load_3(s + 21);
    int64_t s9 = 2097151 & (load_4(s + 23) >> 5);
    int64_t s10 = 2097151 & (load_3(s + 26) >> 2);
    int64_t s11 = 2097151 & (load_4(s + 28) >> 7);
    int64_t s12 = 2097151 & (load_4(s + 31) >> 4);
    int64_t s13 = 2097151 & (load_3(s + 34) >> 1);
    int64_t s14 = 2097151 & (load_4(s + 36) >> 6);
    int64_t s15 = 2097151 & (load_3(s + 39) >> 3);
    int64_t s16 = 2097151 & load_3(s + 42);
    int64_t s17 = 2097151 & (load_4(s + 44) >> 5);
    int64_t s18 = 2097151 & (load_3(s + 47) >> 2);
    int64_t s19 = 2097151 & (load_4(s + 49) >> 7);
    int64_t s20 = 2097151 & (load_4(s + 52) >> 4);
    int64_t s21 = 2097151 & (load_3(s + 55) >> 1);
    int64_t s22 = 2097151 & (load_4(s + 57) >> 6);
    int64_t s23 = (load_4(s + 60) >> 3);

    s11 += s23 * 666643;
    s12 += s23 * 470296;
    s13 += s23 * 654183;
    s14 -= s23 * 997805;
    s15 += s23 * 136657;
    s16 -= s23 * 683901;
    s23 = 0;

    s10 += s22 * 666643;
    s11 += s22 * 470296;
    s12 += s22 * 654183;
    s13 -= s22 * 997805;
    s14 += s22 * 136657;
    s15 -= s22 * 683901;
    s22 = 0;

    s9 += s21 * 666643;
    s10 += s21 * 470296;
    s11 += s21 * 654183;
    s12 -= s21 * 997805;
    s13 += s21 * 136657;
    s14 -= s21 * 683901;
    s21 = 0;

    s8 += s20 * 666643;
    s9 += s20 * 470296;
    s10 += s20 * 654183;
    s11 -= s20 * 997805;
    s12 += s20 * 136657;
    s13 -= s20 * 683901;
    s20 = 0;

    s7 += s19 * 666643;
    s8 += s19 * 470296;
    s9 += s19 * 654183;
    s10 -= s19 * 997805;
    s11 += s19 * 136657;
    s12 -= s19 * 683901;
    s19 = 0;

    s6 += s18 * 666643;
    s7 += s18 * 470296;
    s8 += s18 * 654183;
    s9 -= s18 * 997805;
    s10 += s18 * 136657;
    s11 -= s18 * 683901;
    s18 = 0;

    s5 += s17 * 666643;
    s6 += s17 * 470296;
    s7 += s17 * 654183;
    s8 -= s17 * 997805;
    s9 += s17 * 136657;
    s10 -= s17 * 683901;
    s17 = 0;

    s4 += s16 * 666643;
    s5 += s16 * 470296;
    s6 += s16 * 654183;
    s7 -= s16 * 997805;
    s8 += s16 * 136657;
    s9 -= s16 * 683901;
    s16 = 0;

    s3 += s15 * 666643;
    s4 += s15 * 470296;
    s5 += s15 * 654183;
    s6 -= s15 * 997805;
    s7 += s15 * 136657;
    s8 -= s15 * 683901;
    s15 = 0;

    s2 += s14 * 666643;
    s3 += s14 * 470296;
    s4 += s14 * 654183;
    s5 -= s14 * 997805;
    s6 += s14 * 136657;
    s7 -= s14 * 683901;
    s14 = 0;

    s1 += s13 * 666643;
    s2 += s13 * 470296;
    s3 += s13 * 654183;
    s4 -= s13 * 997805;
    s5 += s13 * 136657;
    s6 -= s13 * 683901;
    s13 = 0;

    s0 += s12 * 666643;
    s1 += s12 * 470296;
    s2 += s12 * 654183;
    s3 -= s12 * 997805;
    s4 += s12 * 136657;
    s5 -= s12 * 683901;
    s12 = 0;

    int64_t carry0 = (s0 + (1 << 20)) >> 21; s1 += carry0; s0 -= carry0 << 21;
    int64_t carry1 = (s1 + (1 << 20)) >> 21; s2 += carry1; s1 -= carry1 << 21;
    int64_t carry2 = (s2 + (1 << 20)) >> 21; s3 += carry2; s2 -= carry2 << 21;
    int64_t carry3 = (s3 + (1 << 20)) >> 21; s4 += carry3; s3 -= carry3 << 21;
    int64_t carry4 = (s4 + (1 << 20)) >> 21; s5 += carry4; s4 -= carry4 << 21;
    int64_t carry5 = (s5 + (1 << 20)) >> 21; s6 += carry5; s5 -= carry5 << 21;
    int64_t carry6 = (s6 + (1 << 20)) >> 21; s7 += carry6; s6 -= carry6 << 21;
    int64_t carry7 = (s7 + (1 << 20)) >> 21; s8 += carry7; s7 -= carry7 << 21;
    int64_t carry8 = (s8 + (1 << 20)) >> 21; s9 += carry8; s8 -= carry8 << 21;
    int64_t carry9 = (s9 + (1 << 20)) >> 21; s10 += carry9; s9 -= carry9 << 21;
    int64_t carry10 = (s10 + (1 << 20)) >> 21; s11 += carry10; s10 -= carry10 << 21;
    int64_t carry11 = (s11 + (1 << 20)) >> 21; s12 += carry11; s11 -= carry11 << 21;

    s0 += s12 * 666643;
    s1 += s12 * 470296;
    s2 += s12 * 654183;
    s3 -= s12 * 997805;
    s4 += s12 * 136657;
    s5 -= s12 * 683901;
    s12 = 0;

    carry0 = (s0 + (1 << 20)) >> 21; s1 += carry0; s0 -= carry0 << 21;
    carry1 = (s1 + (1 << 20)) >> 21; s2 += carry1; s1 -= carry1 << 21;
    carry2 = (s2 + (1 << 20)) >> 21; s3 += carry2; s2 -= carry2 << 21;
    carry3 = (s3 + (1 << 20)) >> 21; s4 += carry3; s3 -= carry3 << 21;
    carry4 = (s4 + (1 << 20)) >> 21; s5 += carry4; s4 -= carry4 << 21;
    carry5 = (s5 + (1 << 20)) >> 21; s6 += carry5; s5 -= carry5 << 21;
    carry6 = (s6 + (1 << 20)) >> 21; s7 += carry6; s6 -= carry6 << 21;
    carry7 = (s7 + (1 << 20)) >> 21; s8 += carry7; s7 -= carry7 << 21;
    carry8 = (s8 + (1 << 20)) >> 21; s9 += carry8; s8 -= carry8 << 21;
    carry9 = (s9 + (1 << 20)) >> 21; s10 += carry9; s9 -= carry9 << 21;
    carry10 = (s10 + (1 << 20)) >> 21; s11 += carry10; s10 -= carry10 << 21;

    s[0] = (uint8_t)(s0 >> 0);
    s[1] = (uint8_t)(s0 >> 8);
    s[2] = (uint8_t)((s0 >> 16) | (s1 << 5));
    s[3] = (uint8_t)(s1 >> 3);
    s[4] = (uint8_t)(s1 >> 11);
    s[5] = (uint8_t)((s1 >> 19) | (s2 << 2));
    s[6] = (uint8_t)(s2 >> 6);
    s[7] = (uint8_t)((s2 >> 14) | (s3 << 7));
    s[8] = (uint8_t)(s3 >> 1);
    s[9] = (uint8_t)(s3 >> 9);
    s[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
    s[11] = (uint8_t)(s4 >> 4);
    s[12] = (uint8_t)(s4 >> 12);
    s[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
    s[14] = (uint8_t)(s5 >> 7);
    s[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
    s[16] = (uint8_t)(s6 >> 2);
    s[17] = (uint8_t)(s6 >> 10);
    s[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
    s[19] = (uint8_t)(s7 >> 5);
    s[20] = (uint8_t)(s7 >> 13);
    s[21] = (uint8_t)(s8 >> 0);
    s[22] = (uint8_t)(s8 >> 8);
    s[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
    s[24] = (uint8_t)(s9 >> 3);
    s[25] = (uint8_t)(s9 >> 11);
    s[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
    s[27] = (uint8_t)(s10 >> 6);
    s[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
    s[29] = (uint8_t)(s11 >> 1);
    s[30] = (uint8_t)(s11 >> 9);
    s[31] = (uint8_t)(s11 >> 17);
}

static const uint8_t SC_L[32] = {
    0xED, 0xD3, 0xF5, 0x5C, 0x1A, 0x63, 0x12, 0x58,
    0xD6, 0x9C, 0xF7, 0xA2, 0xDE, 0xF9, 0xDE, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

static int sc_check(const uint8_t s[32])
{
    uint8_t c = 0;
    int gt = 0;
    for (int i = 31; i >= 0; i--) {
        if (s[i] > SC_L[i] && c == 0) {
            gt = 1;
        }
        if (s[i] < SC_L[i]) {
            c = 1;
        }
    }
    return gt;
}

/* ---- Ed25519 verification -------------------------------------------- */

bool
crypto_ed25519_verify(const uint8_t signature[64], const uint8_t hash[32])
{
    if (sc_check(signature + 32)) {
        return false;
    }

    ge_p3 A;
    if (ge_frombytes(&A, ZK_PUBKEY) != 0) {
        return false;
    }

    ge_p3 B;
    if (ge_frombytes(&B, ED25519_BASEPOINT) != 0) {
        return false;
    }

    uint8_t hram[64];
    sha512_three(signature, 32, ZK_PUBKEY, 32, hash, 32, hram);
    sc_reduce(hram);

    ge_p3 SB, hA;
    ge_scalarmult(&SB, &B, signature + 32);
    ge_scalarmult(&hA, &A, hram);

    fe51_neg(&hA.X, &hA.X);
    fe51_neg(&hA.T, &hA.T);

    ge_p3 Rcalc;
    ge_add(&Rcalc, &SB, &hA);

    uint8_t rcheck[32];
    ge_tobytes(rcheck, &Rcalc);

    return crypto_verify_32(rcheck, signature) == 0;
}