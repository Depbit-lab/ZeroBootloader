#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "protocol.h"
#include "flash_ops.h"
#include "crypto_ops.h"
#include "boot_config.h"

#define REG8(addr)   (*(volatile uint8_t *)(addr))
#define REG16(addr)  (*(volatile uint16_t *)(addr))
#define REG32(addr)  (*(volatile uint32_t *)(addr))

#define PM_BASE                 (0x40000400u)
#define GCLK_BASE               (0x40000C00u)
#define OSCCTRL_BASE            (0x40001000u)
#define NVMCTRL_BASE            (0x41004000u)
#define NVMCTRL_OTP4            (0x00806020u)
#define NVMCTRL_OTP5            (0x00806024u)

#define PM_CPUSEL               REG8(PM_BASE + 0x08u)

#define GCLK_CTRL               REG8(GCLK_BASE + 0x00u)
#define GCLK_STATUS             REG8(GCLK_BASE + 0x01u)
#define GCLK_GENDIV             REG32(GCLK_BASE + 0x08u)
#define GCLK_GENCTRL            REG32(GCLK_BASE + 0x04u)
#define GCLK_CLKCTRL            REG16(GCLK_BASE + 0x02u)

#define GCLK_STATUS_SYNCBUSY    (1u << 7)
#define GCLK_CTRL_SWRST         (1u << 0)

#define GCLK_GENDIV_ID(v)       ((uint32_t)(v) & 0xFu)
#define GCLK_GENDIV_DIV(v)      ((uint32_t)(v) << 8)

#define GCLK_GENCTRL_ID(v)      ((uint32_t)(v) & 0xFu)
#define GCLK_GENCTRL_SRC(v)     ((uint32_t)(v) << 8)
#define GCLK_GENCTRL_GENEN      (1u << 16)
#define GCLK_GENCTRL_RUNSTDBY   (1u << 21)

#define GCLK_CLKCTRL_ID(v)      ((uint16_t)(v) & 0x3Fu)
#define GCLK_CLKCTRL_GEN(v)     ((uint16_t)(v) << 8)
#define GCLK_CLKCTRL_CLKEN      (1u << 14)

#define GCLK_GEN_GCLK0          (0u)
#define GCLK_GEN_GCLK1          (1u)
#define GCLK_ID_DFLL48          (0u)

#define GCLK_SRC_XOSC32K        (0x06u)
#define GCLK_SRC_DFLL48M        (0x07u)

#define OSCCTRL_PCLKSR          REG32(OSCCTRL_BASE + 0x0Cu)
#define OSCCTRL_XOSC32K         REG32(OSCCTRL_BASE + 0x14u)
#define OSCCTRL_DFLLCTRL        REG16(OSCCTRL_BASE + 0x24u)
#define OSCCTRL_DFLLVAL         REG32(OSCCTRL_BASE + 0x2Cu)
#define OSCCTRL_DFLLMUL         REG32(OSCCTRL_BASE + 0x28u)

#define OSCCTRL_PCLKSR_XOSC32KRDY   (1u << 1)
#define OSCCTRL_PCLKSR_DFLLRDY      (1u << 4)
#define OSCCTRL_PCLKSR_DFLLLCKF     (1u << 5)
#define OSCCTRL_PCLKSR_DFLLLCKC     (1u << 6)

#define OSCCTRL_XOSC32K_ENABLE     (1u << 1)
#define OSCCTRL_XOSC32K_XTALEN     (1u << 2)
#define OSCCTRL_XOSC32K_EN32K      (1u << 3)
#define OSCCTRL_XOSC32K_STARTUP(v) ((uint32_t)(v) << 8)

#define OSCCTRL_DFLLCTRL_ENABLE    (1u << 1)
#define OSCCTRL_DFLLCTRL_MODE      (1u << 2)
#define OSCCTRL_DFLLCTRL_WAITLOCK  (1u << 7)
#define OSCCTRL_DFLLCTRL_CCDIS     (1u << 8)
#define OSCCTRL_DFLLCTRL_BPLCKC    (1u << 10)

#define NVMCTRL_CTRLB             REG16(NVMCTRL_BASE + 0x04u)
#define NVMCTRL_CTRLB_RWS(v)      ((uint16_t)(v) & 0xFu)

#define SCB_VTOR   (*(volatile unsigned long *)0xE000ED08)

static inline void __disable_irq(void) { __asm volatile ("cpsid i"); }
static inline void __set_MSP(uint32_t topOfMainStack) {
    __asm volatile ("msr msp, %0" : : "r" (topOfMainStack) : );
}

void usb_init(void);
void usb_task(void);
int usb_cdc_getchar(void);
void usb_cdc_write(const uint8_t *data, size_t len);
extern uint32_t usb_cdc_get_baud(void);

static bool check_bootloader_entry(void);
void jump_to_application(uint32_t app_addr);

static void
wait_for_gclk_sync(void)
{
    while ((GCLK_STATUS & GCLK_STATUS_SYNCBUSY) != 0u) {
    }
}

static void
wait_for_dfll_ready(void)
{
    while ((OSCCTRL_PCLKSR & OSCCTRL_PCLKSR_DFLLRDY) == 0u) {
    }
}

static void
system_clock_init_arduino_zero(void)
{
    NVMCTRL_CTRLB = (NVMCTRL_CTRLB & ~0xFu) | NVMCTRL_CTRLB_RWS(1u);

    PM_CPUSEL = 0u;

    GCLK_CTRL = GCLK_CTRL_SWRST;
    wait_for_gclk_sync();

    OSCCTRL_XOSC32K = OSCCTRL_XOSC32K_STARTUP(6u) |
                      OSCCTRL_XOSC32K_XTALEN |
                      OSCCTRL_XOSC32K_EN32K |
                      OSCCTRL_XOSC32K_ENABLE;
    while ((OSCCTRL_PCLKSR & OSCCTRL_PCLKSR_XOSC32KRDY) == 0u) {
    }

    GCLK_GENDIV = GCLK_GENDIV_ID(GCLK_GEN_GCLK1) | GCLK_GENDIV_DIV(1u);
    wait_for_gclk_sync();

    GCLK_GENCTRL = GCLK_GENCTRL_ID(GCLK_GEN_GCLK1) |
                   GCLK_GENCTRL_SRC(GCLK_SRC_XOSC32K) |
                   GCLK_GENCTRL_GENEN;
    wait_for_gclk_sync();

    GCLK_CLKCTRL = GCLK_CLKCTRL_ID(GCLK_ID_DFLL48) |
                   GCLK_CLKCTRL_GEN(GCLK_GEN_GCLK1) |
                   GCLK_CLKCTRL_CLKEN;
    wait_for_gclk_sync();

    OSCCTRL_DFLLCTRL = 0u;
    wait_for_dfll_ready();

    const uint32_t coarse_cal = (*(volatile uint32_t *)NVMCTRL_OTP4 >> 26) & 0x3Fu;
    const uint32_t fine_cal = (*(volatile uint32_t *)NVMCTRL_OTP5) & 0x3FFu;
    uint32_t coarse = coarse_cal;
    if (coarse == 0x3Fu) {
        coarse = 0x1Fu;
    }
    const uint32_t fine = fine_cal;

    OSCCTRL_DFLLVAL = (coarse << 26) | fine;
    wait_for_dfll_ready();

    const uint32_t dfllmul = (31u << 26) | (511u << 16) | 1465u;
    OSCCTRL_DFLLMUL = dfllmul;
    wait_for_dfll_ready();

    OSCCTRL_DFLLCTRL = OSCCTRL_DFLLCTRL_WAITLOCK |
                       OSCCTRL_DFLLCTRL_BPLCKC |
                       OSCCTRL_DFLLCTRL_CCDIS |
                       OSCCTRL_DFLLCTRL_MODE |
                       OSCCTRL_DFLLCTRL_ENABLE;

    while (((OSCCTRL_PCLKSR & OSCCTRL_PCLKSR_DFLLLCKC) == 0u) ||
           ((OSCCTRL_PCLKSR & OSCCTRL_PCLKSR_DFLLLCKF) == 0u)) {
    }

    GCLK_GENDIV = GCLK_GENDIV_ID(GCLK_GEN_GCLK0) | GCLK_GENDIV_DIV(1u);
    wait_for_gclk_sync();

    GCLK_GENCTRL = GCLK_GENCTRL_ID(GCLK_GEN_GCLK0) |
                   GCLK_GENCTRL_SRC(GCLK_SRC_DFLL48M) |
                   GCLK_GENCTRL_GENEN |
                   GCLK_GENCTRL_RUNSTDBY;
    wait_for_gclk_sync();
}

static bool
check_bootloader_entry(void)
{
    if (usb_cdc_get_baud() == 1200U) {
        return true;
    }

    const uint32_t *magic = (const uint32_t *)(APP_START_ADDRESS - 4);
    if (*magic != APP_VALID_MAGIC) {
        return true;
    }

    return false;
}

void
jump_to_application(uint32_t app_addr)
{
    __disable_irq();

    SCB_VTOR = app_addr;

    uint32_t sp = *((uint32_t *)app_addr);
    uint32_t pc = *((uint32_t *)(app_addr + 4));

    __set_MSP(sp);

    void (*app_reset_handler)(void) = (void (*)(void))pc;
    app_reset_handler();

    while (1) {
    }
}

int
main(void)
{
    system_clock_init_arduino_zero();

    if (!check_bootloader_entry()) {
        jump_to_application(APP_START_ADDRESS);
    }

    usb_init();
    flash_init();
    protocol_init();

    for (;;) {
        usb_task();
        int c = usb_cdc_getchar();
        if (c >= 0) {
            protocol_process_char((uint8_t)c);
        }
    }
}
