ROMCC      ?= tools/romcc
ROMCCFLAGS ?= -mcpu=i386 -O -I./include
CC         ?= gcc
CFLAGS     ?= -m32 -march=i586 -mno-sse -mno-mmx -O2 -Wall -Wextra -fno-builtin \
              -fno-pic -fno-pie -fno-stack-protector -fcf-protection=none -fno-asynchronous-unwind-tables \
              -fno-unwind-tables -fno-tree-loop-distribute-patterns -I./include
AS         ?= as
LD         ?= ld
OBJCOPY    ?= objcopy

# normal build files, executed in RAM with stack
SRCS = src/elf.c
SRCS+= src/ext2.c
SRCS+= src/heap.c
SRCS+= src/ide.c
SRCS+= src/lib.c
SRCS+= src/linuxboot.c
SRCS+= src/mbr.c
SRCS+= src/seabiosboot.c
SRCS+= src/stage2.c 

# objects of normal build objects, plus the ROM resident code (init+stage1)
OBJS = $(patsubst src/%.c, build/%.o, $(SRCS)) build/init.o build/seabios.o build/vgabios.o

all: fgt60.rom

tools/romcc: tools/romcc.c
	$(CC) -std=gnu89 -fcommon -fgnu89-inline -fno-stack-protector -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -o $@ $<

build:
	mkdir -p build

build/vgabios.o: vga_1023_8500.rom
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

build/seabios.o: seabios.elf
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

# stage 1 + tail: romcc (no RAM, no stack)
build/stage1.inc: src/stage1.c $(ROMCC) | build
	$(ROMCC) $(ROMCCFLAGS) --label-prefix=s1 -o $@ $<

build/init.o: src/init.S build/stage1.inc
	$(AS) --32 -I build -o $@ $<

build/init.elf: $(OBJS) | build
	$(LD) -m elf_i386 -z noexecstack -T link.ld -o $@ $^

fgt60.rom: build/init.elf
	$(OBJCOPY) -O binary --gap-fill 0xff $< $@

flash: fgt60.rom
	minipro -p sst39sf020a@plcc32 -w $<

monitor: flash
	cat /dev/ttyUSB0

launch: fgt60.rom
	qemu-system-i386 -bios $< -m 128M -serial stdio -hda hd.img

clean:
	rm -rf build tools/romcc fgt60.rom

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
