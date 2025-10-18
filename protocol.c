/*
 * protocol.c - Text command protocol implementation
 *
 * Implements a simple line oriented parser for the ZeroKey USB
 * bootloader protocol.  Supported commands are:
 *   HELLO\n            -> replies with OK BOOT vX.Y\n
 *   ERASE APP\n       -> erases the application region
 *   WRITE <addr> <len> <crc32>\n
 *                     followed by <len> binary bytes to program.
 *                     After all bytes are received the block CRC is
 *                     verified and the firmware SHA‑256 hash is updated.
 *   DONE <signature_hex>\n -> verifies the Ed25519 signature of the
 *                     firmware hash and jumps to the application on
 *                     success.
 *
 * The parser is intentionally simple and does not allocate large
 * buffers.  Binary data is written to flash page by page to
 * conserve RAM.  CRC32 (IEEE 802.3) is calculated incrementally.
 */

#include "protocol.h"
#include "flash_ops.h"
#include "boot_config.h"
#include "crypto_ops.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

/* External functions provided by main.c or the USB layer */
extern void usb_cdc_write(const uint8_t *data, size_t len);
extern void jump_to_application(uint32_t app_addr);

/* Bootloader version string */
#define BOOT_VERSION_MAJOR 1
#define BOOT_VERSION_MINOR 0

/* Maximum length for a single command line (excluding binary data). */
#define CMD_BUF_SIZE 128

/* Internal parser state */
static enum {
    STATE_WAIT_CMD,  /* waiting for a newline‑terminated text command */
    STATE_WRITE_DATA /* receiving binary data for WRITE command        */
} parser_state;

static char   cmd_buffer[CMD_BUF_SIZE]; /* command line buffer */
static size_t cmd_index;

/* Variables used during a WRITE command */
static uint32_t write_addr;
static uint32_t write_length;
static uint32_t write_expected_crc;
static size_t   write_received;
static uint32_t crc_accum;
static uint8_t  page_buffer[FLASH_ROW_SIZE];
static size_t   page_index;

/* CRC32 calculation using the standard polynomial (0xEDB88320).  The
 * algorithm starts with crc_accum initialised to 0xFFFFFFFF and ends
 * with a final XOR with 0xFFFFFFFF. */
static uint32_t
crc32_update(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

static uint32_t
crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

/* Send a zero‑terminated string back to the host. */
static void
send_str(const char *s)
{
    usb_cdc_write((const uint8_t *)s, strlen(s));
}

void
protocol_init(void)
{
    parser_state   = STATE_WAIT_CMD;
    cmd_index      = 0;
    cmd_buffer[0]  = '\0';
    write_addr     = 0;
    write_length   = 0;
    write_expected_crc = 0;
    write_received = 0;
    crc_accum      = 0xFFFFFFFFUL;
    page_index     = 0;
    /* Initialise crypto hash context */
    crypto_sha256_init();
}

/* Parse and execute a completed text command line.  This function
 * handles command dispatch and argument validation. */
static void
handle_command(void)
{
    /* Trim any trailing carriage return */
    size_t len = strlen(cmd_buffer);
    while (len > 0 && (cmd_buffer[len - 1] == '\r' || cmd_buffer[len - 1] == '\n')) {
        cmd_buffer[--len] = '\0';
    }
    /* HELLO */
    if (strcmp(cmd_buffer, "HELLO") == 0) {
        char reply[32];
        snprintf(reply, sizeof(reply), "OK BOOT v%d.%d\n", BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR);
        send_str(reply);
        return;
    }
    /* ERASE APP */
    if (strcmp(cmd_buffer, "ERASE APP") == 0) {
        flash_erase_application();
        /* Reset hash context when erasing */
        crypto_sha256_init();
        send_str("OK ERASE\n");
        return;
    }
    /* WRITE */
    if (strncmp(cmd_buffer, "WRITE ", 6) == 0) {
        /* Extract three arguments: address, length and CRC32.  strtoul
         * accepts both decimal and hexadecimal (0x…) values. */
        char *args = cmd_buffer + 6;
        char *addr_str = strtok(args, " ");
        char *len_str  = strtok(NULL, " ");
        char *crc_str  = strtok(NULL, " ");
        if (!addr_str || !len_str || !crc_str) {
            send_str("ERR FORMAT\n");
            return;
        }
        uint32_t addr = (uint32_t)strtoul(addr_str, NULL, 0);
        uint32_t length = (uint32_t)strtoul(len_str, NULL, 0);
        uint32_t crc    = (uint32_t)strtoul(crc_str, NULL, 0);
        /* Validate address and length.  Ensure the write stays within
         * the application region.  The end of flash is defined by
         * FLASH_SIZE in flash_ops.h.
         *
         * NOTE: APP_START_ADDRESS is defined in main.c.  To ensure
         * this file can compile independently (e.g. in unit tests),
         * define a fallback value when it is not provided via
         * compilation flags.  The default corresponds to an 8 KiB
         * bootloader region (0x0000–0x1FFF). */
        if (addr < APP_START_ADDRESS || (addr + length) > FLASH_SIZE) {
            send_str("ERR PARAM\n");
            return;
        }
        /* Initialise write state */
        write_addr        = addr;
        write_length      = length;
        write_expected_crc= crc;
        write_received    = 0;
        crc_accum         = 0xFFFFFFFFUL;
        page_index        = 0;
        parser_state      = STATE_WRITE_DATA;
        /* No reply is sent yet; the response occurs after the data block is
         * received and the CRC has been checked. */
        return;
    }
    /* DONE */
    if (strncmp(cmd_buffer, "DONE ", 5) == 0) {
        /* The signature is provided as a 128‑character hex string.
         * Convert it into 64 binary bytes. */
        const char *sig_hex = cmd_buffer + 5;
        size_t sig_hex_len = strlen(sig_hex);
        if (sig_hex_len != 128) {
            send_str("ERR FORMAT\n");
            return;
        }
        uint8_t signature[64];
        bool ok = true;
        for (size_t i = 0; i < 64; i++) {
            char tmp[3] = { sig_hex[i * 2], sig_hex[i * 2 + 1], '\0' };
            char *endp;
            unsigned long v = strtoul(tmp, &endp, 16);
            if (*endp != '\0' || v > 0xFFUL) {
                ok = false;
                break;
            }
            signature[i] = (uint8_t)v;
        }
        if (!ok) {
            send_str("ERR FORMAT\n");
            return;
        }
        /* Finalise the SHA‑256 hash of the firmware */
        uint8_t digest[32];
        crypto_sha256_final(digest);
        /* Verify the Ed25519 signature over the hash using the
         * compiled‑in public key. */
        if (crypto_ed25519_verify(signature, digest)) {
            send_str("OK DONE\n");
            /* Mark the application as valid and jump to it.  flash_ops
             * implements flash_set_app_valid_flag() to write a magic
             * value in the word immediately before APP_START_ADDRESS. */
            flash_set_app_valid_flag();
            jump_to_application(APP_START_ADDRESS);
        } else {
            send_str("ERR SIGNATURE\n");
        }
        return;
    }
    /* Unknown command */
    send_str("ERR UNKNOWN\n");
}

void
protocol_process_char(uint8_t c)
{
    if (parser_state == STATE_WRITE_DATA) {
        /* Accumulate binary bytes until the expected number of bytes
         * has been received.  Update CRC and SHA‑256 incrementally. */
        crc_accum = crc32_update(crc_accum, c);
        crypto_sha256_update(&c, 1);
        /* Buffer the data until we have a full page or until the end
         * of the block.  Flash writes must be aligned to a page size
         * of 64 bytes on the SAMD21【582320296557380†L4648-L4654】; four pages form
         * an erase row【744331242867409†L156-L160】, but the flash driver handles
         * page writes transparently. */
        page_buffer[page_index++] = c;
        write_received++;
        if (page_index >= FLASH_PAGE_SIZE) {
            flash_write(write_addr, page_buffer, page_index);
            write_addr += page_index;
            page_index = 0;
        }
        /* When the entire block has been received we perform CRC
         * verification and send the response. */
        if (write_received >= write_length) {
            /* Write any remaining buffered data */
            if (page_index > 0) {
                flash_write(write_addr, page_buffer, page_index);
                write_addr += page_index;
                page_index = 0;
            }
            /* Finalise CRC */
            uint32_t crc_final = crc32_finalize(crc_accum);
            if (crc_final == write_expected_crc) {
                send_str("OK WRITE\n");
            } else {
                /* CRC mismatch – the block was written anyway.  The
                 * host can re‑send the block if necessary. */
                send_str("ERR CRC\n");
            }
            /* Reset state to accept the next command */
            parser_state = STATE_WAIT_CMD;
            write_length   = 0;
            write_received = 0;
            crc_accum      = 0xFFFFFFFFUL;
        }
        return;
    }
    /* STATE_WAIT_CMD: accumulate characters until a newline */
    if (c == '\n') {
        /* Complete command received */
        handle_command();
        /* Clear buffer for next command */
        cmd_index = 0;
        cmd_buffer[0] = '\0';
    } else {
        /* Ignore carriage returns */
        if (c == '\r') {
            return;
        }
        /* Append to command buffer if space remains */
        if (cmd_index < (CMD_BUF_SIZE - 1)) {
            cmd_buffer[cmd_index++] = (char)c;
            cmd_buffer[cmd_index]   = '\0';
        } else {
            /* Overflow – reset the buffer to avoid undefined behaviour */
            cmd_index = 0;
            cmd_buffer[0] = '\0';
        }
    }
}