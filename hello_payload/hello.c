
#include <stdint.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;

    outb(COM1, c);
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

void payload_main(void)
{
    serial_puts("\r\n");
    serial_puts("==============================\r\n");
    serial_puts(" Hello from standalone ELF!\r\n");
    serial_puts(" Running on the FortiGate 60\r\n");
    serial_puts("==============================\r\n");

    for (;;)
        __asm__ volatile ("hlt");
}
