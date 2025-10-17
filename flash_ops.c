/*
 * flash_ops.c - SAMD21 flash helper stubs
 *
 * These routines encapsulate the low level operations required to
 * erase and write the ATSAMD21G18A flash memory.  They deliberately
 * omit the full implementation in order to keep the bootloader
 * compact.  For a real bootloader the functions below must interact
 * with the NVM controller (NVMCTRL) registers as described in the
 * datasheet.  The erase granularity is one row (256 bytes) and the
 * write granularity is one 64‑byte page【744331242867409†L156-L160】.  See
 * Microchip’s datasheet and the ASF NVM driver for reference.
 */

#include "flash_ops.h"
#include "boot_config.h"
#include <stdint.h>
#include <stddef.h>

/* Reference to the start of application for erasing and programming */
extern uint32_t __flash_start__;

/* Stub: Configure the NVM controller for manual write mode and set
 * wait states based on CPU frequency.  On the SAMD21 the NVMCTRL
 * CTRLB.MANW bit must be set for manual page writes.  The number of
 * wait states depends on the clock; see the datasheet. */
void
flash_init(void)
{
    /* Platform specific initialisation.  Typically you would set
     * NVMCTRL->CTRLB.MANW = 1 and configure NVMCTRL->CTRLB.RWS
     * according to the CPU frequency. */
}

/* Erase the application region by iterating over rows from
 * APP_START_ADDRESS to FLASH_SIZE.  Each row must be erased before it
 * can be re‑written【744331242867409†L238-L249】. */
void
flash_erase_application(void)
{
    uint32_t addr = APP_START_ADDRESS;
    while (addr < FLASH_SIZE) {
        flash_erase_range(addr, FLASH_ROW_SIZE);
        addr += FLASH_ROW_SIZE;
    }
}

/* Erase a contiguous range of flash.  In a real implementation this
 * loops over rows, issues the NVM row erase command (ER) and waits
 * for the READY flag. */
void
flash_erase_range(uint32_t addr, size_t len)
{
    /* Align address down to a row boundary */
    uint32_t row_addr = addr & ~(FLASH_ROW_SIZE - 1U);
    uint32_t end_addr = addr + (uint32_t)len;
    while (row_addr < end_addr) {
        /* TODO: implement NVM row erase here */
        /* For example:
         * NVMCTRL->ADDR.reg = row_addr / 2;
         * NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
         * while (!NVMCTRL->INTFLAG.bit.READY);
         */
        row_addr += FLASH_ROW_SIZE;
    }
}

/* Write one or more pages to flash.  addr must be aligned to a page
 * boundary and len must be a multiple of 4 bytes.  Before writing
 * each page the page buffer must be cleared (PBC command) and then
 * NVMCTRL->CTRLA.WP triggered.  This stub simply copies data into
 * the address for simulation purposes. */
void
flash_write(uint32_t addr, const uint8_t *data, size_t len)
{
    /* In a real implementation you must: clear the page buffer using
     * PBC command, write words to the page buffer, then issue the
     * write page command (WP).  Wait for READY after each command.
     * Here we simply perform a direct memory copy because the code
     * executes from flash and this stub will be replaced by proper
     * NVMCTRL operations. */
    uint32_t *dest = (uint32_t *)addr;
    const uint32_t *src = (const uint32_t *)data;
    size_t words = (len + 3U) / 4U;
    while (words--) {
        *dest++ = *src++;
    }
}

/* Write APP_VALID_MAGIC into the word preceding the application start
 * address.  This flag is used by main.c to decide whether a valid
 * application exists.  The row containing this word must have been
 * erased already. */
void
flash_set_app_valid_flag(void)
{
    uint32_t addr = APP_START_ADDRESS - 4U;
    uint32_t magic = APP_VALID_MAGIC;
    flash_write(addr, (const uint8_t *)&magic, sizeof(magic));
}