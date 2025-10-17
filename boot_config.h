#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdint.h>

/*
 * boot_config.h - Shared bootloader configuration constants
 *
 * Centralises application layout definitions used by multiple modules.
 */

/*
 * Address where the main application image begins. The first 16 KiB of
 * flash (0x0000 - 0x3FFF) are reserved for the bootloader, so the
 * application vectors and code start at 0x4000.
 */
#define APP_START_ADDRESS 0x00004000UL

#endif /* BOOT_CONFIG_H */
