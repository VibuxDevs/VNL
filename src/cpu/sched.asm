; VNL Scheduler stubs
; These bypass the generic ISR infrastructure so we can swap RSP
; (and thus switch tasks) between PUSH_REGS and POP_REGS.
;
; PUSH_REGS / POP_REGS MUST be kept in sync with isr_stubs.asm.
BITS 64

extern sched_timer_c   ; uint64_t sched_timer_c(Registers *r)
extern sched_yield_c   ; uint64_t sched_yield_c(Registers *r)
extern sched_pending_cr3

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
    xor  rax, rax
    mov  ax, ds
    push rax
    mov  ax, es
    push rax
    mov  ax, fs
    push rax
    mov  ax, gs
    push rax
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
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

; ---- Timer stub (hardware IRQ0, vector 32) -------------------------
; Calls sched_timer_c(Registers*).  If it returns non-zero, swap RSP.
global sched_timer_stub
sched_timer_stub:
    push qword 0        ; dummy err_code
    push qword 32       ; int_no
    PUSH_REGS
    mov  rdi, rsp       ; arg: Registers *
    call sched_timer_c  ; returns new RSP or 0
    test rax, rax
    jz   .no_switch
    mov  rbx, [rel sched_pending_cr3]
    mov  cr3, rbx
    mov  rsp, rax       ; *** context switch ***
.no_switch:
    POP_REGS
    add  rsp, 16        ; discard int_no + err_code
    iretq

; ---- Yield stub (software INT 0x81) --------------------------------
global sched_yield_stub
sched_yield_stub:
    push qword 0
    push qword 0x81
    PUSH_REGS
    mov  rdi, rsp
    call sched_yield_c
    test rax, rax
    jz   .no_switch
    mov  rbx, [rel sched_pending_cr3]
    mov  cr3, rbx
    mov  rsp, rax
.no_switch:
    POP_REGS
    add  rsp, 16
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
