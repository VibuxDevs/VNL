/*
 * Userspace display server stub: mmap /dev/fb0, fill screen (VNL syscalls via int $0x80).
 */
.text
.global _start
_start:
    subq $512, %rsp
    movq $3, %rax
    leaq devpath(%rip), %rdi
    movq $3, %rsi
    int $0x80
    cmpq $0, %rax
    jl fail
    movq %rax, %r12

    movq $16, %rax
    movq %r12, %rdi
    movq $0x4602, %rsi
    movq %rsp, %rdx
    int $0x80

    movl 24(%rsp), %esi
    testl %esi, %esi
    jz fail

    movq $9, %rax
    xorq %rdi, %rdi
    movq $3, %rdx
    movq $1, %r10
    movq %r12, %r8
    xorq %r9, %r9
    int $0x80
    cmpq $0, %rax
    jl fail
    movq %rax, %r13

    cld
    movl 24(%rsp), %ecx
    shrl $2, %ecx
    movq %r13, %rdi
    movl $0xFF5080C0, %eax
    rep stosl

    movq $60, %rax
    xorq %rdi, %rdi
    int $0x80
fail:
    movq $60, %rax
    movq $1, %rdi
    int $0x80

.section .rodata
devpath:
    .asciz "/dev/fb0"
