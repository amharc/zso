.text

.globl raw_syscall
    .type raw_syscall, @function

raw_syscall:
    .cfi_startproc
    pushl %ebp
    .cfi_def_cfa_offset 8
    movl %esp, %ebp

    pushl %ebx
    .cfi_def_cfa_offset 12
    pushl %esi
    .cfi_def_cfa_offset 16
    pushl %edi
    .cfi_def_cfa_offset 20

    movl 8(%ebp), %eax
    movl 12(%ebp), %ebx
    movl 16(%ebp), %ecx
    movl 20(%ebp), %edx
    movl 24(%ebp), %esi
    movl 28(%ebp), %edi
    movl 32(%ebp), %ebp

    int $0x80

    popl %edi
    .cfi_def_cfa_offset 16
    popl %esi
    .cfi_def_cfa_offset 12
    popl %ebx
    .cfi_def_cfa_offset 8
    popl %ebp
    .cfi_def_cfa_offset 4

    ret
    .cfi_endproc
