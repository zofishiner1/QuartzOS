ENTRY(start)

SECTIONS {
    . = 0x100000;  /* Ядро будет загружено в память начиная с 1MB */

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }
}
