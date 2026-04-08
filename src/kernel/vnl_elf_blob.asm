; Embedded userspace ELF (build/vnl-x.elf). Linked into kernel for /usr/bin/Xorg.
section .note.GNU-stack noalloc nowrite progbits
section .rodata
global vnl_x_elf
global vnl_x_elf_end
vnl_x_elf:
    incbin "build/vnl-x.elf"
vnl_x_elf_end:
