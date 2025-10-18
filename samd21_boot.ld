/*
 * samd21_boot.ld - Minimal linker script for the ZeroBootloader build
 *
 * Targets the Microchip ATSAMD21G18A (Cortex-M0+). The bootloader
 * occupies the first 8 KiB of flash while the remaining flash is used
 * by the user application (starting at 0x00002000 as defined in
 * boot_config.h). SRAM is 32 KiB beginning at 0x20000000.
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 8K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .vectors ORIGIN(FLASH) :
  {
    KEEP(*(.vectors))
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
  } > FLASH

  .ARM.extab :
  {
    *(.ARM.extab*)
  } > FLASH

  __exidx_start = .;
  .ARM.exidx :
  {
    *(.ARM.exidx*)
  } > FLASH
  __exidx_end = .;

  _etext = .;

  .data : AT(_etext)
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    *(.ramfunc*)
    . = ALIGN(4);
    _edata = .;
  } > RAM

  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  . = ALIGN(4);
  _end = .;
  PROVIDE(end = .);
}
