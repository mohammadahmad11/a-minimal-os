all: kernel.bin disk.img

boot.o: boot.asm
	nasm -f elf32 boot.asm -o boot.o

kernel.o: kernel.c
	gcc -m32 -ffreestanding -c kernel.c -o kernel.o

kernel.bin: boot.o kernel.o linker.ld
	ld -m elf_i386 -T linker.ld -o kernel.bin boot.o kernel.o

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=20480

run: kernel.bin disk.img
	qemu-system-i386 -kernel kernel.bin -hda disk.img

clean:
	rm -f *.o kernel.bin disk.img