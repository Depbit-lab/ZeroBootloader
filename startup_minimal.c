// startup_minimal.c — vector table + Reset_Handler mínimos para SAMD21 (Cortex-M0+)
// Sin CMSIS. No usa cabeceras externas.

#include <stdint.h>

// Fin de RAM del SAMD21G18A (32 KB desde 0x20000000)
#define RAM_START   0x20000000UL
#define RAM_SIZE    (32UL * 1024UL)
#define ESTACK      (RAM_START + RAM_SIZE)

// Prototipos
void Reset_Handler(void);
void Default_Handler(void);

// Declarar main() (está en main.c)
int main(void);

// Tabla de vectores (ubicada al inicio del bootloader)
__attribute__((section(".vectors")))
const void *vector_table[] = {
  (void *)ESTACK,      // Initial MSP
  (void *)Reset_Handler, // Reset
  (void *)Default_Handler, // NMI
  (void *)Default_Handler, // HardFault
  // El M0+ tiene menos excepciones; rellenamos con Default_Handler
  (void *)Default_Handler, (void *)Default_Handler, (void *)Default_Handler,
  (void *)Default_Handler, (void *)Default_Handler, (void *)Default_Handler,
  (void *)Default_Handler, (void *)Default_Handler, (void *)Default_Handler,
  (void *)Default_Handler, (void *)Default_Handler, (void *)Default_Handler
};

// Handlers débiles para interrupciones (todas a Default_Handler)
void __attribute__((weak)) Default_Handler(void) {
  while (1) { __asm volatile ("nop"); }
}

// Pequeña rutina de copia/zero de .data/.bss (opcional mínima)
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

static void init_data_bss(void) {
  uint32_t *src = &_sidata;
  uint32_t *dst = &_sdata;
  while (dst < &_edata) { *dst++ = *src++; }
  dst = &_sbss;
  while (dst < &_ebss)  { *dst++ = 0; }
}

// Reset: init básica y salto a main()
void __attribute__((noreturn)) Reset_Handler(void) {
  init_data_bss();
  // Aquí podrías configurar relojes básicos si lo necesitas.
  (void)main();
  while (1) { }
}
