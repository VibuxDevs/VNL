; VNL — CPU low-level routines (GDT/IDT reload, port I/O, MSRs)
BITS 64

section .text

; void gdt_flush(GDTPointer *ptr)
global gdt_flush
gdt_flush:
    lgdt [rdi]
    ; Reload segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Far return to reload CS with code selector 0x08
    pop rax
    push qword 0x08
    push rax
    retfq

; void idt_flush(IDTPointer *ptr)
global idt_flush
idt_flush:
    lidt [rdi]
    ret

; void tss_flush(uint16_t sel)
global tss_flush
tss_flush:
    ltr di
    ret

; ---- Port I/O (used by drivers) -----------------------------------
global outb
outb:
    mov rdx, rdi    ; port
    mov rax, rsi    ; value
    out dx, al
    ret

global inb
inb:
    mov rdx, rdi
    xor rax, rax
    in  al, dx
    ret

global outw
outw:
    mov rdx, rdi
    mov rax, rsi
    out dx, ax
    ret

global inw
inw:
    mov rdx, rdi
    xor rax, rax
    in  ax, dx
    ret

global outl
outl:
    mov rdx, rdi
    mov rax, rsi
    out dx, eax
    ret

global inl
inl:
    mov rdx, rdi
    xor rax, rax
    in  eax, dx
    ret

; void io_wait(void) — tiny delay via unused port
global io_wait
io_wait:
    out 0x80, al
    ret

; ---- CPUID --------------------------------------------------------
; void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
;            uint32_t *ecx, uint32_t *edx)
global cpuid_query
cpuid_query:
    push rbx
    mov  eax, edi
    xor  ecx, ecx
    cpuid
    mov  [rsi],  eax
    mov  [rdx],  ebx
    mov  [rcx],  ecx
    mov  [r8],   edx
    pop  rbx
    ret

; ---- MSR ----------------------------------------------------------
; uint64_t rdmsr(uint32_t msr)
global rdmsr_q
rdmsr_q:
    mov ecx, edi
    rdmsr
    shl rdx, 32
    or  rax, rdx
    ret

; void wrmsr(uint32_t msr, uint64_t val)
global wrmsr_q
wrmsr_q:
    mov ecx, edi
    mov rax, rsi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret

; ---- Interrupt helpers --------------------------------------------
global sti_asm
sti_asm:
    sti
    ret

global cli_asm
cli_asm:
    cli
    ret

global halt_loop
halt_loop:
    cli
.l: hlt
    jmp .l

; ---- Read control registers (for VMM) ----------------------------
global read_cr2
read_cr2:
    mov rax, cr2
    ret

global read_cr3
read_cr3:
    mov rax, cr3
    ret

global write_cr3
write_cr3:
    mov cr3, rdi
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
