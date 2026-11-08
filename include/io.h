#ifndef _IO_H_
#define _IO_H_

#include <stdint.h>

/* IO port access functions */
#if defined(__ROMCC__)
/* Under romcc, use its built-in hardware primitives */
static inline void outb(uint8_t value, uint16_t port)
{
    __builtin_outb(value, port);
}

static inline void outw(uint16_t value, uint16_t port)
{
    __builtin_outw(value, port);
}

static inline void outl(uint32_t value, uint16_t port)
{
    __builtin_outl(value, port);
}

static inline uint8_t inb(uint16_t port)
{
    return __builtin_inb(port);
}

static inline uint16_t inw(uint16_t port)
{
    return __builtin_inw(port);
}

static inline uint32_t inl(uint16_t port)
{
    return __builtin_inl(port);
}
#else
/* Under standard GCC, use standard x86 extended constraints */
static inline void outb(uint8_t value, uint16_t port)
{
    __asm__ __volatile__ ("outb %b0, %w1" : : "a" (value), "Nd" (port));
}

static inline void outw(uint16_t value, uint16_t port)
{
    __asm__ __volatile__ ("outw %w0, %w1" : : "a" (value), "Nd" (port));
}

static inline void outl(uint32_t value, uint16_t port)
{
    __asm__ __volatile__ ("outl %0, %w1" : : "a" (value), "Nd" (port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ __volatile__ ("inb %w1, %b0" : "=a"(value) : "Nd" (port));
	return value;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t value;
    __asm__ __volatile__ ("inw %w1, %w0" : "=a"(value) : "Nd" (port));
    return value;
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ __volatile__ ("inl %w1, %0" : "=a"(value) : "Nd" (port));
    return value;
}
#endif

/* PCI device address macro and register access functions */
#define PCI_DEV(b, d, f, r) (((b)<<16)|((d)<<11)|((f)<<8)|(r))

static inline void pci_write8(uint32_t a, uint8_t v)
{
	outl(0x80000000|(a&~3), 0xcf8);
	outb(v, 0xcfc+(a&3));
}

static inline void pci_write16(uint32_t a, uint16_t v)
{
	outl(0x80000000|(a&~3), 0xcf8);
	outw(v, 0xcfc+(a&3));
}

static inline void pci_write32(uint32_t a, uint32_t v)
{
	outl(0x80000000|(a&~3), 0xcf8);
	outl(v, 0xcfc);
}

static inline uint8_t pci_read8(uint32_t a)
{
	outl(0x80000000|(a&~3), 0xcf8);
	return inb(0xcfc+(a&3));
}

static inline uint32_t pci_read32(uint32_t a)
{
	outl(0x80000000|(a&~3), 0xcf8);
	return inl(0xcfc+(a&3));
}

/* UART output functions */
void uart_putc(uint8_t c)
{
	while(!(inb(0x3fd)&0x20));
	outb(c, 0x3f8);
}

void uart_puts(const char *s)
{
	while(*s)
		uart_putc(*s++);
}

void uart_hex32(uint32_t v)
{
	uint32_t i, n;

	for(i=0;i<8;i++)
	{
		n=v>>28;

		if(n<10)
			uart_putc('0'+n);
		else
			uart_putc('a'+n-10);

		v=v<<4;
	}
}

#endif
