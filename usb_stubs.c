// usb_stubs.c — stubs mínimos para poder enlazar
#include <stddef.h>
#include <stdint.h>

// Inicializa el USB CDC (stub)
void usb_init(void) {}

// Atiende tareas USB (stub)
void usb_task(void) {}

// Lee un byte del CDC; -1 si no hay datos (stub)
int usb_cdc_getchar(void) {
    return -1;
}

// Escribe al CDC (stub: ignora la salida)
void usb_cdc_write(const uint8_t *data, size_t len) {
    (void)data; (void)len;
}
