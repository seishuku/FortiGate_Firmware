/*
 * Minimal coreboot table for libpayload payloads (FILO).
 *
 * libpayload's lib_get_sysinfo() scans 0x0-0xFFF and 0xF0000-0xF0FFF for a 16-byte aligned "LBIO"
 * header, verifies the header and table checksums, then walks the records.  This writes the smallest
 * table that gives FILO a memory map and a serial port.  Call cbtable_build() right before jumping
 * to the payload.
 */
#include <stddef.h>
#include <stdint.h>

extern void *memset(void *, int, size_t);

#define CB_TABLE_ADDR 0x00000500 /* 16-byte aligned, inside the first 4 KB libpayload scans */

#define CB_TAG_MEMORY 0x0001
#define CB_TAG_SERIAL 0x000f

#define CB_MEM_RAM 1
#define CB_MEM_RESERVED 2

#define CB_SERIAL_TYPE_IO 1

/* Match whatever the UART is actually running at. */
#define SERIAL_IOBASE 0x3f8
#define SERIAL_BAUD 115200
#define SERIAL_INPUT_HZ 1843200

typedef struct cb_header
{
	uint8_t signature[4]; /* "LBIO" */
	uint32_t header_bytes;
	uint32_t header_checksum;
	uint32_t table_bytes;
	uint32_t table_checksum;
	uint32_t table_entries;
} __attribute__((packed)) cb_header;

typedef struct cb_record
{
	uint32_t tag;
	uint32_t size; /* whole record including this header, multiple of 8 */
} __attribute__((packed)) cb_record;

typedef struct cb_memory_range
{
	uint64_t start;
	uint64_t size;
	uint32_t type;
} __attribute__((packed)) cb_memory_range;

typedef struct cb_serial
{
	struct cb_record rec;
	uint32_t type;
	uint32_t baseaddr;
	uint32_t baud;
	uint32_t regwidth;
	uint32_t input_hertz;
} __attribute__((packed)) cb_serial;

/* Same algorithm as libpayload's compute_ip_checksum(): ones-complement 16-bit sum, inverted. */
static uint32_t ip_checksum(const void *addr, uint32_t length)
{
	const uint8_t *ptr = addr;
	uint32_t sum = 0;
	uint32_t i;

	for(i = 0; i < length; i++)
	{
		uint32_t v = ptr[i];
		if(i & 1)
			v <<= 8;
		sum += v;
		if(sum > 0xffff)
			sum = (sum + (sum >> 16)) & 0xffff;
	}

	return (~sum) & 0xffff;
}

static uint8_t *add_memory_record(uint8_t *p, uint32_t ram_bytes)
{
	struct cb_record *rec = (struct cb_record *)p;
	struct cb_memory_range *r = (struct cb_memory_range *)(p + sizeof(*rec));

	/* 0x0000-0x0FFF   BDA + this table
	 * 0x1000-0x9FFFF  low RAM (stage 2 / .bss / stack are finished with by now)
	 * 0xA0000-0xFFFFF VGA, option ROMs, legacy BIOS area
	 * 1 MiB - top     RAM */
	r[0].start = 0x00000000;
	r[0].size = 0x00001000;
	r[0].type = CB_MEM_RESERVED;
	r[1].start = 0x00001000;
	r[1].size = 0x0009f000;
	r[1].type = CB_MEM_RAM;
	r[2].start = 0x000a0000;
	r[2].size = 0x00060000;
	r[2].type = CB_MEM_RESERVED;
	r[3].start = 0x00100000;
	r[3].size = ram_bytes - 0x00100000;
	r[3].type = CB_MEM_RAM;

	rec->tag = CB_TAG_MEMORY;
	rec->size = sizeof(*rec) + 4 * sizeof(struct cb_memory_range); /* 88, already a multiple of 8 */

	return p + rec->size;
}

static uint8_t *add_serial_record(uint8_t *p)
{
	struct cb_serial *s = (struct cb_serial *)p;
	uint32_t size = (sizeof(*s) + 7) & ~7u;

	memset(p, 0, size);
	s->rec.tag = CB_TAG_SERIAL;
	s->rec.size = size;
	s->type = CB_SERIAL_TYPE_IO;
	s->baseaddr = SERIAL_IOBASE;
	s->baud = SERIAL_BAUD;
	s->regwidth = 1;
	s->input_hertz = SERIAL_INPUT_HZ;

	return p + size;
}

void cbtable_build(uint32_t ram_bytes)
{
	struct cb_header *hdr = (struct cb_header *)CB_TABLE_ADDR;
	uint8_t *table = (uint8_t *)CB_TABLE_ADDR + sizeof(*hdr);
	uint8_t *p = table;

	memset(hdr, 0, sizeof(*hdr));

	p = add_memory_record(p, ram_bytes);
	p = add_serial_record(p);

	hdr->signature[0] = 'L';
	hdr->signature[1] = 'B';
	hdr->signature[2] = 'I';
	hdr->signature[3] = 'O';
	hdr->header_bytes = sizeof(*hdr);
	hdr->table_bytes = p - table;
	hdr->table_entries = 2;
	hdr->table_checksum = ip_checksum(table, hdr->table_bytes);
	hdr->header_checksum = 0;
	hdr->header_checksum = ip_checksum(hdr, sizeof(*hdr));
}
