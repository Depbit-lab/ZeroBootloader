// --- Helpers mínimos sin CMSIS para Cortex-M0+ (SAMD21) ---
#include <stdint.h>  // <- necesario para uint32_t

#define SCB_VTOR   (*(volatile unsigned long *)0xE000ED08)
static inline void __disable_irq(void){ __asm volatile ("cpsid i"); }
static inline void __set_MSP(uint32_t topOfMainStack){
  __asm volatile ("msr msp, %0" : : "r" (topOfMainStack) : );
}
// -----------------------------------------------------------



/*
 * main.c - Custom bootloader for ATSAMD21G18A
 *
 * This file contains the top‑level bootloader entry point and ties
 * together the protocol parser, flash operations and cryptographic
 * verification. The code is intentionally lightweight and keeps all
 * global state in static variables to minimise RAM usage. Low level
 * functions such as USB initialisation, CDC serial handling and the
 * 1200 baud “touch” detection are left as stubs – these are highly
 * platform specific and must be implemented using the appropriate
 * SAMD21 USB library or ASF/Arduino core. See the documentation
 * referenced in the README for details on how opening a SAMD21
 * virtual serial port at 1200 bps forces the chip into bootloader
 * mode【247177836008116†L524-L532】.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "protocol.h"
#include "flash_ops.h"
#include "crypto_ops.h"
#include "boot_config.h"

/* Forward declarations */
static bool check_bootloader_entry(void);
void jump_to_application(uint32_t app_addr);

/* USB CDC stub API.  These are minimal prototypes for sending and
 * receiving bytes over the virtual serial port.  In a real
 * implementation these functions must call into the SAMD21 USB stack
 * and handle buffering, endpoint servicing and timeouts.  See
 * Microchip’s ASF or the Arduino core for examples. */
void usb_init(void);
void usb_task(void);
int  usb_cdc_getchar(void);        /* returns next byte or -1 if none */
void usb_cdc_write(const uint8_t *data, size_t len);
extern uint32_t usb_cdc_get_baud(void);

/*
 * Check whether the bootloader should remain active.  The typical
 * SAMD21 bootloaders can be triggered by rapidly double tapping the
 * reset button or by opening the USB serial port at 1200 bps and
 * closing it again【247177836008116†L524-L532】.  This stub always returns
 * true when there is no valid application present.  Platforms that
 * support a 1200 baud touch should implement the detection here.
 */
static bool
check_bootloader_entry(void)
{
    /* Detect a 1200 baud "touch" from the host which signals that the
     * bootloader should remain active regardless of application state. */
    if (usb_cdc_get_baud() == 1200U) {
        return true;
    }

    /* Read a magic value written by flash_set_app_valid_flag().  If the
     * value is present then an application has been successfully
     * programmed and verified.  Otherwise the bootloader will stay
     * resident.  In a real implementation you may also check the
     * 1200 bps touch condition or a dedicated GPIO button. */
    const uint32_t *magic = (const uint32_t *)(APP_START_ADDRESS - 4);
    if (*magic != APP_VALID_MAGIC) {
        return true; /* no valid app, stay in bootloader */
    }

    return false;
}

/*
 * Transfer control to the user application.  This function resets the
 * vector table offset, initialises the stack pointer from the
 * application’s first word and jumps to the application reset
 * handler.  All interrupts are disabled before the jump.  This
 * implementation is Cortex‑M0+ specific. */
void
jump_to_application(uint32_t app_addr)
{
    /* Ensure interrupts are disabled during the jump */
    __disable_irq();
    /* Deinitialise peripherals here if necessary */
    
    /* Set the vector table to the application region */
    SCB_VTOR = app_addr;
    /* Load the application’s initial stack pointer and entry point */
    uint32_t sp  = *((uint32_t *)app_addr);
    uint32_t pc  = *((uint32_t *)(app_addr + 4));
    /* Set the MSP and jump */
    __set_MSP(sp);
    void (*app_reset_handler)(void) = (void (*)(void))pc;
    app_reset_handler();
    /* Should never return */
    while (1) {
        ;
    }
}

int main(void)
{
    /* Initialise microcontroller clocks, WDT etc.  This is hardware
     * specific and omitted here for brevity. */

    /* Decide whether to jump directly to the existing application or
     * enter bootloader mode. */
    if (!check_bootloader_entry()) {
        jump_to_application(APP_START_ADDRESS);
    }

    /* Initialise USB CDC.  When the SAMD21 enumerates it will
     * appear as two COM ports – one for the bootloader and one for
     * sketches.  The protocol parser runs on the bootloader port. */
    usb_init();
    /* Configure the flash controller for manual write/erase.  flash_init()
     * sets the necessary wait states and MANW bit but is left as a stub
     * for this example. */
    flash_init();
    /* Initialise protocol parser and cryptographic state */
    protocol_init();

    /* Main bootloader loop.  This loop services the USB tasks and
     * feeds incoming characters into the protocol parser. */
    for (;;) {
        /* Keep the USB device stack alive */
        usb_task();
        /* Pull a byte from the CDC RX buffer if available.  The
         * parser operates on individual characters to minimise memory
         * usage. */
        int c = usb_cdc_getchar();
        if (c >= 0) {
            protocol_process_char((uint8_t)c);
        }
        /* Optionally add a timeout or watchdog refresh here. */
    }
}