/*
 * crypto_ops.h - Cryptographic helpers for the bootloader
 *
 * This header exposes a minimal interface for hashing the firmware
 * image using SHA‑256 and verifying an Ed25519 signature against a
 * built‑in public key.  The actual implementations are stubbed out
 * to reduce the bootloader footprint; users must provide real
 * cryptographic routines appropriate for their platform.  SHA‑256 is
 * specified by FIPS 180‑4 and produces a 256‑bit (32 byte) digest.
 * Ed25519 is an Edwards‑curve Digital Signature Algorithm based on
 * Curve25519.  The public key below is provided by the project
 * requirements and must not be altered.
 */

#ifndef CRYPTO_OPS_H
#define CRYPTO_OPS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Public key for Ed25519 signature verification.  This 32‑byte
 * constant is hardcoded into the bootloader and represents the
 * trusted signer.  When a firmware update is received the bootloader
 * computes the SHA‑256 hash of the binary and verifies the Ed25519
 * signature transmitted by the host against this key.  Only if the
 * signature is valid will the bootloader mark the application as
 * authentic and transfer control to it. */
static const uint8_t ZK_PUBKEY[32] = {
    0xEA, 0x4D, 0x85, 0x32, 0xDB, 0x8F, 0xC5, 0x70,
    0xE8, 0xA3, 0xC6, 0xD9, 0x4C, 0x8F, 0x41, 0x29,
    0xBE, 0x91, 0x13, 0xD5, 0xB6, 0xF3, 0x51, 0x50,
    0xD2, 0xD3, 0xE6, 0x7F, 0x62, 0x80, 0x49, 0x7B
};

/* Initialise the SHA‑256 hash context.  Must be called before
 * updating the hash with firmware bytes.  In a real implementation
 * this would set up the state variables (h0–h7) defined in the
 * standard. */
void crypto_sha256_init(void);

/* Update the SHA‑256 hash with a chunk of data.  The function may be
 * called multiple times; the hash accumulates over the entire
 * firmware image.  Real implementations process data in 64‑byte
 * blocks and update the internal state accordingly. */
void crypto_sha256_update(const uint8_t *data, size_t len);

/* Finalise the SHA‑256 hash and write the 32‑byte digest into
 * `digest`.  After calling this function the context is reset and
 * cannot be reused.  Real implementations perform padding and length
 * encoding as per FIPS 180‑4. */
void crypto_sha256_final(uint8_t digest[32]);

/* Verify an Ed25519 signature.  Returns true if the signature is
 * valid for the provided message hash and the built‑in public key,
 * false otherwise.  A real implementation would perform scalar
 * multiplication on the curve and compare points.  The bootloader
 * uses this to authenticate firmware before jumping to it. */
bool crypto_ed25519_verify(const uint8_t signature[64], const uint8_t hash[32]);

#endif /* CRYPTO_OPS_H */