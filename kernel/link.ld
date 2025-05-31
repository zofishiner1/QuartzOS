ENTRY(start)

SECTIONS {
    . = 0x100000;
    .multiboot : { *(.multiboot) }
    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    .data : { *(.data) }
    .bss : {
        *(.bss)
        . = ALIGN(16);
        stack_bottom = .;
        . += 16K;
        stack_top = .;
    }
}