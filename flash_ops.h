/*
 * flash_ops.h - Flash memory helper routines for ATSAMD21G18A
 *
 * The ATSAMD21 flash is organised into pages and rows.  Each row
 * contains four 64‑byte pages【744331242867409†L156-L160】 and the page size is 64
 * bytes【582320296557380†L4648-L4654】.  Writes are performed one page at a
 * time, while erases happen at row granularity【744331242867409†L238-L249】.  This
 * module provides simplified interfaces for erasing and writing the
 * application region of flash as used by the bootloader.
 */

#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>
#include <stddef.h>

/* Flash size of the ATSAMD21G18A in bytes. */
#define FLASH_SIZE     (256UL * 1024UL)
/* Page size in bytes (64 bytes)【582320296557380†L4648-L4654】. */
#define FLASH_PAGE_SIZE 64U
/* Row size is four pages (256 bytes)【744331242867409†L156-L160】. */
#define FLASH_ROW_SIZE  (FLASH_PAGE_SIZE * 4U)

/* Application valid magic number stored immediately before APP_START. */
#define APP_VALID_MAGIC 0x55AA13F0UL

/* Initialise the flash controller.  Configures the NVM for manual
 * write mode and sets appropriate wait states. */
void flash_init(void);

/* Erase the entire application region from APP_START_ADDRESS up to the
 * end of flash.  The bootloader region must not be erased. */
void flash_erase_application(void);

/* Erase a contiguous range of flash starting at addr with length
 * bytes.  Erases whole rows that intersect the range. */
void flash_erase_range(uint32_t addr, size_t len);

/* Write an arbitrary number of bytes to flash at the specified
 * address.  The address must be page aligned (multiple of
 * FLASH_PAGE_SIZE) and len must not cross a row boundary.  This
 * function does not perform an erase – ensure the row is erased
 * beforehand. */
void flash_write(uint32_t addr, const uint8_t *data, size_t len);

/* Mark the application as valid by writing APP_VALID_MAGIC into the
 * word immediately preceding the application start address.  The row
 * containing this word must be erased first. */
void flash_set_app_valid_flag(void);

#endif /* FLASH_OPS_H */