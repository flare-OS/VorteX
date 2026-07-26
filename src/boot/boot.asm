BITS 32

extern kernel_main

global start

section .multiboot
align 8
multiboot2_header:
dd 0xe85250d6

dd 0

dd multiboot2_header_end - multiboot2_header

dd 0x100000000 - (0xe85250d6 + 0 + (multiboot2_header_end - multiboot2_header))

; End tag

dw 0

dw 0
dd 8
multiboot2_header_end:

section .text
start:
    cli
    mov esp, stack_top
    push ebx
    push eax
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
resb 16384
stack_top:
