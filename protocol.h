/*
 * protocol.h - Command parser for the SAMD21 bootloader
 *
 * This header declares the functions used to initialise the protocol
 * state machine and to feed received characters into the parser.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Initialise the protocol parser.  Must be called once before
 * processing any characters.  Resets the internal SHA‑256 context
 * and prepares the flash for new data. */
void protocol_init(void);

/* Process a single character from the host.  The parser operates
 * character by character to minimise memory usage.  Commands are
 * newline terminated and binary data for WRITE operations follows
 * immediately after the command. */
void protocol_process_char(uint8_t c);

#endif /* PROTOCOL_H */