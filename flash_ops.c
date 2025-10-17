/*
 * flash_ops.c - SAMD21 flash helper routines
 *
 * These routines encapsulate the low level operations required to
 * erase and write the ATSAMD21G18A flash memory via the NVM controller
 * (NVMCTRL).  The erase granularity is one row (256 bytes) and the
 * write granularity is one 64‑byte page【744331242867409†L156-L160】.
 */

#include "flash_ops.h"
#include "boot_config.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Minimal SAMD21 NVMCTRL register definitions                               */
/* ------------------------------------------------------------------------- */

/* Base address of the SAMD21 non-volatile memory controller */
#define NVMCTRL_BASE            (0x41004000UL)

/* Register access helpers */
#define NVMCTRL_REG16(offset)   (*(volatile uint16_t *)((NVMCTRL_BASE) + (offset)))
#define NVMCTRL_REG32(offset)   (*(volatile uint32_t *)((NVMCTRL_BASE) + (offset)))
#define NVMCTRL_REG8(offset)    (*(volatile uint8_t  *)((NVMCTRL_BASE) + (offset)))

/* Individual registers used by the bootloader */
#define NVMCTRL_CTRLA_REG       NVMCTRL_REG16(0x00U)
#define NVMCTRL_CTRLB_REG       NVMCTRL_REG16(0x04U)
#define NVMCTRL_INTFLAG_REG     NVMCTRL_REG8(0x14U)
#define NVMCTRL_ADDR_REG        NVMCTRL_REG32(0x1CU)

/* Bit definitions */
#define NVMCTRL_INTFLAG_READY   (1U << 0)

#define NVMCTRL_CTRLB_RWS_Pos   1U
#define NVMCTRL_CTRLB_RWS_Msk   (0xFU << NVMCTRL_CTRLB_RWS_Pos)
#define NVMCTRL_CTRLB_MANW      (1U << 7)

/* Command key and opcodes */
#define NVMCTRL_CTRLA_CMDEX_KEY (0xA5U << 8)
#define NVMCTRL_CTRLA_CMD_ER    0x02U
#define NVMCTRL_CTRLA_CMD_PBC   0x44U
#define NVMCTRL_CTRLA_CMD_WP    0x04U

/* Low level helpers ------------------------------------------------------ */
static inline void
nvm_wait_ready(void)
{
    while ((NVMCTRL_INTFLAG_REG & NVMCTRL_INTFLAG_READY) == 0U) {
        /* Wait for command completion */
    }
}

static inline void
nvm_exec_cmd(uint16_t cmd)
{
    NVMCTRL_CTRLA_REG = NVMCTRL_CTRLA_CMDEX_KEY | cmd;
    nvm_wait_ready();
}

void
flash_init(void)
{
    nvm_wait_ready();

    /* Enable manual write mode and configure wait states for 48 MHz */
    NVMCTRL_CTRLB_REG |= NVMCTRL_CTRLB_MANW;
    NVMCTRL_CTRLB_REG = (uint16_t)((NVMCTRL_CTRLB_REG & ~NVMCTRL_CTRLB_RWS_Msk) |
                                   (1U << NVMCTRL_CTRLB_RWS_Pos));
    nvm_wait_ready();
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

/* Erase a contiguous range of flash rows using the NVMCTRL ER command. */
void
flash_erase_range(uint32_t addr, size_t len)
{
    /* Align address down to a row boundary */
    uint32_t row_addr = addr & ~(FLASH_ROW_SIZE - 1U);
    uint32_t end_addr = addr + (uint32_t)len;
    if ((end_addr < addr) || (end_addr > FLASH_SIZE)) {
        end_addr = FLASH_SIZE;
    }
    if (len == 0U) {
        return;
    }

    while (row_addr < end_addr) {
        nvm_wait_ready();
        NVMCTRL_ADDR_REG = row_addr / 2U;
        nvm_exec_cmd(NVMCTRL_CTRLA_CMD_ER);
        row_addr += FLASH_ROW_SIZE;
    }
}

/* Program one or more flash pages.  Each page is staged via the NVM page
 * buffer, written as 32-bit words, then committed with the WP command. */
void
flash_write(uint32_t addr, const uint8_t *data, size_t len)
{
    size_t remaining = len;

    while (remaining > 0U) {
        size_t chunk = remaining < FLASH_PAGE_SIZE ? remaining : FLASH_PAGE_SIZE;
        union {
            uint8_t b[FLASH_PAGE_SIZE];
            uint32_t w[FLASH_PAGE_SIZE / sizeof(uint32_t)];
        } page_buffer;

        memset(page_buffer.b, 0xFF, sizeof(page_buffer.b));
        memcpy(page_buffer.b, data, chunk);

        nvm_wait_ready();
        nvm_exec_cmd(NVMCTRL_CTRLA_CMD_PBC);

        volatile uint32_t *dest = (volatile uint32_t *)addr;
        const uint32_t *src = page_buffer.w;
        for (size_t i = 0; i < (FLASH_PAGE_SIZE / sizeof(uint32_t)); ++i) {
            dest[i] = src[i];
        }

        NVMCTRL_ADDR_REG = addr / 2U;
        nvm_exec_cmd(NVMCTRL_CTRLA_CMD_WP);

        addr += FLASH_PAGE_SIZE;
        data += chunk;
        remaining -= chunk;
    }
}

/* Write APP_VALID_MAGIC into the word preceding the application start
 * address.  This flag is used by main.c to decide whether a valid
 * application exists.  The row containing this word must have been
 * erased already. */
void
flash_set_app_valid_flag(void)
{
    uint32_t flag_addr = APP_START_ADDRESS - sizeof(uint32_t);
    uint32_t page_addr = flag_addr & ~(FLASH_PAGE_SIZE - 1U);
    uint32_t offset = flag_addr - page_addr;
    union {
        uint8_t b[FLASH_PAGE_SIZE];
        uint32_t w[FLASH_PAGE_SIZE / sizeof(uint32_t)];
    } page_buffer;
    uint32_t magic = APP_VALID_MAGIC;

    memset(page_buffer.b, 0xFF, sizeof(page_buffer.b));
    memcpy(&page_buffer.b[offset], &magic, sizeof(magic));

    flash_write(page_addr, page_buffer.b, FLASH_PAGE_SIZE);
}
