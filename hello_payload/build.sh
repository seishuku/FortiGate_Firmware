CFLAGS="-m32 -march=i586 -ffreestanding \
-fno-pic -fno-pie -fno-stack-protector \
-fno-builtin -fno-asynchronous-unwind-tables \
-fno-unwind-tables -mno-sse -mno-mmx \
-msoft-float -O0"

gcc $CFLAGS -c hello.c -o hello.o

gcc -m32 -c hello.S -o hello_entry.o

ld -m elf_i386 \
   -T hello.ld \
   -o hello.elf \
   hello_entry.o hello.o