bits 32
section .multiboot
    align 4
    dd 0x1BADB002            ; Multiboot magic number
    dd 0x00                  ; Flags
    dd - (0x1BADB002 + 0x00) ; Checksum

section .text
global start
extern kmain

start:
    ; 1. Set up the stack pointer so C functions can use local variables
    mov esp, kernel_stack + 4096   

    call kmain               ; Jump into your C code
    cli
.halt:
    hlt
    jmp .halt

; 2. Reserve uninitialized memory in the .bss section for the stack
section .bss
align 4
kernel_stack:
    resb 4096                ; Reserve 4KB of memory for the stack