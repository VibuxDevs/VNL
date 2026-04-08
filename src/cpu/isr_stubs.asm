; VNL — ISR / IRQ stubs (x86-64)
; Generates 256 interrupt stubs that push a unified Registers frame
; and call the C handler: void isr_handler(Registers *r)
BITS 64

extern isr_handler

; ---- Unified interrupt frame (pushed by stubs, consumed by C) -----
; Order on stack when handler is called (top = low address):
;   r15..rax (general purpose), ds, es, fs, gs,
;   int_no, err_code,
;   rip, cs, rflags, rsp, ss  (auto-pushed by CPU)

%macro PUSH_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    ; Segment registers (as 64-bit values for alignment)
    xor rax, rax
    mov ax, ds
    push rax
    mov ax, es
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
%endmacro

%macro POP_REGS 0
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; ---- ISR stub with no error code (CPU doesn't push one) -----------
%macro ISR_NO_ERR 1
isr_stub_%1:
    push qword 0        ; dummy error code
    push qword %1       ; interrupt number
    PUSH_REGS
    mov rdi, rsp        ; Registers* arg
    call isr_handler
    POP_REGS
    add rsp, 16         ; pop int_no + err_code
    iretq
%endmacro

; ---- ISR stub with error code (CPU pushes one) --------------------
%macro ISR_ERR 1
isr_stub_%1:
    push qword %1       ; interrupt number (err code already on stack)
    PUSH_REGS
    mov rdi, rsp
    call isr_handler
    POP_REGS
    add rsp, 16
    iretq
%endmacro

; Exceptions 0-7 (no error code)
ISR_NO_ERR 0
ISR_NO_ERR 1
ISR_NO_ERR 2
ISR_NO_ERR 3
ISR_NO_ERR 4
ISR_NO_ERR 5
ISR_NO_ERR 6
ISR_NO_ERR 7
; Exception 8 — Double Fault (has error code)
ISR_ERR    8
ISR_NO_ERR 9
; 10-14 have error codes
ISR_ERR    10
ISR_ERR    11
ISR_ERR    12
ISR_ERR    13
ISR_ERR    14
ISR_NO_ERR 15
ISR_NO_ERR 16
ISR_ERR    17
ISR_NO_ERR 18
ISR_NO_ERR 19
ISR_NO_ERR 20
ISR_NO_ERR 21
ISR_NO_ERR 22
ISR_NO_ERR 23
ISR_NO_ERR 24
ISR_NO_ERR 25
ISR_NO_ERR 26
ISR_NO_ERR 27
ISR_NO_ERR 28
ISR_NO_ERR 29
ISR_ERR    30
ISR_NO_ERR 31

; IRQs 0-15 mapped to vectors 32-47
%assign i 32
%rep 16
    ISR_NO_ERR i
    %assign i i+1
%endrep

; Software interrupts 48-255
%assign i 48
%rep 208
    ISR_NO_ERR i
    %assign i i+1
%endrep

; ---- Table of stub addresses (exported to C for IDT setup) --------
section .rodata
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+ i
    %assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
