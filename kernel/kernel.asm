; kernel.asm

section .multiboot
align 4
dd 0x1BADB002 ; магическое число
dd 0x01 ; флаги (только флаг 0x01 для поддержки памяти)
dd - (0x1BADB002 + 0x01) ; контрольная сумма

section .text
global start
extern kmain ; kmain определена во внешнем файле

start:
    cli ; блокировка прерываний
    mov esp, stack_space ; указатель стека
    push eax ; передаем multiboot_structure в kmain
    call kmain
    hlt ; остановка процессора

section .bss
resb 8192 ; 8KB на стек
stack_space:
