#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1

#define ELF_MAGIC 0x464c457f
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_386 3

struct elf32_ehdr {
	uint8_t  ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed));

struct elf32_phdr {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
} __attribute__((packed));

extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);

int elf_load(const void *image, uint32_t image_size, uint32_t ram_size, uint32_t *entry)
{
	const struct elf32_ehdr *eh = image;
	const uint8_t *bytes = image;
	uint32_t i;

	if (!image || !entry || image_size < sizeof(*eh))
		return -1;

	if (*(const uint32_t *)eh->ident != ELF_MAGIC)
		return -2;

	if (eh->ident[4] != ELFCLASS32 || eh->ident[5] != ELFDATA2LSB || eh->type != ET_EXEC || eh->machine != EM_386)
		return -3;

	if (eh->phentsize != sizeof(struct elf32_phdr))
		return -4;

	if (eh->phoff > image_size || eh->phnum > (image_size - eh->phoff) / sizeof(struct elf32_phdr))
		return -5;

	for (i = 0; i < eh->phnum; i++)
	{
		const struct elf32_phdr *ph=(const struct elf32_phdr *)(bytes + eh->phoff +	i * sizeof(struct elf32_phdr));
		uint32_t dst;

		if (ph->type != PT_LOAD)
			continue;

		dst = ph->paddr;

		if (ph->filesz > ph->memsz)
			return -6;

		if (ph->offset > image_size || ph->filesz > image_size - ph->offset)
			return -7;

		if (dst > ram_size || ph->memsz > ram_size - dst)
			return -8;

		if (ph->memsz == 0)
			continue;

		memcpy((void *)dst, bytes + ph->offset, ph->filesz);
		memset((void *)(dst + ph->filesz), 0, ph->memsz - ph->filesz);
	}

	*entry = eh->entry;

	return 0;
}
