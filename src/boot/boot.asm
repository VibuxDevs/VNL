; VNL Boot — Multiboot2 → protected mode → 64-bit long mode → kernel_main
;
; ADDRESSING RULES:
;   .multiboot2 / .boot section:
;       VMA == LMA == physical (~1 MiB, set by linker `. = 1M`)
;       => use labels AS-IS.  NEVER subtract KERNEL_VMA from them.
;
;   .text / .data / .bss sections:
;       VMA = physical + KERNEL_VMA  (high half, AT() in linker.ld)
;       => to get physical addr:  label - KERNEL_VMA
;       => reachable only AFTER paging is enabled

BITS 32

KERNEL_VMA      equ 0xFFFFFFFF80000000
IA32_EFER       equ 0xC0000080
EFER_LME        equ (1 << 8)
CR4_PAE         equ (1 << 5)
CR0_PE          equ (1 << 0)
CR0_PG          equ (1 << 31)
PG_P            equ (1 << 0)
PG_W            equ (1 << 1)
PG_HUGE         equ (1 << 7)
BOOT_STACK_SIZE equ 0x10000     ; 64 KiB

; ======================================================================
; Multiboot2 header
; ======================================================================
section .multiboot2 progbits alloc noexec nowrite align=8
mb2_start:
    dd 0xE85250D6               ; magic
    dd 0                        ; arch: i386
    dd (mb2_end - mb2_start)    ; header length
    dd -(0xE85250D6 + 0 + (mb2_end - mb2_start))  ; checksum
    dw 0                        ; end tag type
    dw 0
    dd 8
mb2_end:

; ======================================================================
; .boot section — VMA = physical = ~1 MiB
; All labels here are already physical addresses.
; ======================================================================
section .boot progbits alloc exec write align=4096

global _start
_start:
    cli
    cld

    ; Save Multiboot2 registers before any call clobbers them.
    ; mb2_save_* are in this same .boot section → physical addresses.
    mov [mb2_save_magic], eax
    mov [mb2_save_info],  ebx

    ; ---- Verify long-mode support (CPUID leaf 80000001h, bit 29) ----
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb  .no_lm

    mov eax, 0x80000001
    cpuid
    bt  edx, 29
    jnc .no_lm

    ; ---- Build page tables (labels are physical here) ---------------
    call setup_paging

    ; ---- Load CR3 = physical address of PML4 ------------------------
    mov eax, pml4               ; pml4 label IS its physical address
    mov cr3, eax

    ; ---- Enable PAE -------------------------------------------------
    mov eax, cr4
    or  eax, CR4_PAE
    mov cr4, eax

    ; ---- Set EFER.LME -----------------------------------------------
    mov ecx, IA32_EFER
    rdmsr
    or  eax, EFER_LME
    wrmsr

    ; ---- Enable paging (activates long mode) ------------------------
    mov eax, cr0
    or  eax, (CR0_PG | CR0_PE)
    mov cr0, eax

    ; ---- Load GDTR --------------------------------------------------
    ; gdt64_ptr.base already contains the physical address of gdt64.
    lgdt [gdt64_ptr]

    ; ---- Far jump to flush pipeline and enter 64-bit mode -----------
    ; long_mode_stub is in .boot (physical), reachable from identity map.
    jmp 0x08:long_mode_stub

.no_lm:
    mov dword [0xB8000], 0x4F4C4F4E   ; "NL" on red VGA
    hlt

; ======================================================================
; setup_paging — zero page tables, then fill with:
;   PML4[0]      → pdpt_low  : identity   0 – 4 GiB  (4 × 1 GiB huge)
;   PML4[511]    → pdpt_high : higher-half mapping
;   pdpt_high[510] → 1 GiB huge page at physical 0
;                   covers KERNEL_VMA … KERNEL_VMA + 1 GiB
; ======================================================================
setup_paging:
    ; Zero pml4 + pdpt_low + pdpt_high (3 × 4096 bytes)
    xor  eax, eax
    mov  edi, pml4
    mov  ecx, (3 * 4096) / 4
    rep  stosd

    ; PML4[0] → pdpt_low
    mov  eax, pdpt_low
    or   eax, (PG_P | PG_W)
    mov  dword [pml4 + 0 * 8], eax
    mov  dword [pml4 + 0 * 8 + 4], 0

    ; PML4[511] → pdpt_high
    mov  eax, pdpt_high
    or   eax, (PG_P | PG_W)
    mov  dword [pml4 + 511 * 8], eax
    mov  dword [pml4 + 511 * 8 + 4], 0

    ; pdpt_low[0..3]: identity map 0–4 GiB with 1 GiB huge pages
    mov  edi, pdpt_low
    mov  eax, (PG_P | PG_W | PG_HUGE)
    xor  ecx, ecx
.id_loop:
    mov  dword [edi], eax
    mov  dword [edi + 4], 0
    add  eax, 0x40000000        ; advance 1 GiB
    add  edi, 8
    inc  ecx
    cmp  ecx, 4
    jl   .id_loop

    ; pdpt_high[510]: 1 GiB huge page, physical base = 0
    ; PDPT index for 0xFFFFFFFF80000000 = (addr >> 30) & 0x1FF = 510
    mov  dword [pdpt_high + 510 * 8],     (PG_P | PG_W | PG_HUGE)
    mov  dword [pdpt_high + 510 * 8 + 4], 0

    ret

; ======================================================================
; 64-bit stub — still in .boot (physical), reached via identity map.
; Paging is now active.
; ======================================================================
BITS 64
long_mode_stub:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Stack lives in .bss (high VMA).  Load its address absolutely.
    mov rsp, (boot_stack_top_phys + KERNEL_VMA)

    ; Pass Multiboot2 data: edi = magic, esi = info ptr (zero-extended)
    ; mb2_save_* are in .boot (still identity-mapped at their physical addr)
    mov edi, dword [mb2_save_magic]
    mov esi, dword [mb2_save_info]   ; zero-extended to rsi

    ; kernel_main is in .text (high VMA).
    ; Distance boot→kernel_main >> 2 GiB → cannot use direct CALL (rel32).
    ; Load the absolute address and call through a register.
    extern kernel_main
    mov  rax, kernel_main
    call rax

.halt:
    cli
    hlt
    jmp .halt

; ======================================================================
; GDT (in .boot, so VMA = physical — gdt64_ptr.base is the physical addr)
; ======================================================================
BITS 32
align 8
gdt64:
    dq 0                        ; null
    dq 0x00AF9A000000FFFF       ; 0x08 kernel code 64-bit DPL0
    dq 0x00AF92000000FFFF       ; 0x10 kernel data 64-bit DPL0
gdt64_end:

gdt64_ptr:
    dw (gdt64_end - gdt64 - 1)
    dq gdt64                    ; physical address (VMA == phys for .boot)

; Saved Multiboot2 values
mb2_save_magic: dd 0
mb2_save_info:  dd 0

; ======================================================================
; Page tables — 4 KiB aligned, pre-zeroed in image.
; setup_paging re-fills them at runtime.
; ======================================================================
align 4096
pml4:      times 4096 db 0
pdpt_low:  times 4096 db 0
pdpt_high: times 4096 db 0

; ======================================================================
; Boot stack — in .bss (high VMA).
; We reference the physical address as (label - KERNEL_VMA) at build time,
; but add KERNEL_VMA back in the 64-bit stub once paging is active so the
; stack pointer is in the properly mapped higher-half region.
; ======================================================================
section .bss nobits alloc noexec write align=16
    resb BOOT_STACK_SIZE
boot_stack_top_phys equ ($ - KERNEL_VMA)   ; constant for the 64-bit stub
global boot_stack_top
boot_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
