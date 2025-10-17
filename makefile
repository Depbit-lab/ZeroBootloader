# Makefile — Bootloader SAMD21G18A (Cortex-M0+), 16 KB

TARGET  = samd21_bootloader
MCU     = cortex-m0plus

CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

CFLAGS  = -Os -mcpu=$(MCU) -mthumb -ffunction-sections -fdata-sections \
          -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables \
          -Wall -Wextra -Wno-unused-parameter
LDFLAGS = -nostartfiles -nostdlib -Wl,--gc-sections -Tsamd21_boot.ld -Wl,-Map=$(TARGET).map

SRC = startup_minimal.c \
      main.c protocol.c flash_ops.c crypto_ops.c \
      usb_stubs.c


OBJ = $(SRC:.c=.o)

all: $(TARGET).bin

$(TARGET).elf: $(OBJ) samd21_boot.ld
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	del /Q *.o *.elf *.bin *.map 2>NUL || true
