        .text
        .globl  __chkstk
        .def    __chkstk; .scl 2; .type 32; .endef
__chkstk:
        pushq   %rcx
        pushq   %rax
        leaq    24(%rsp), %rcx
        cmpq    $0x1000, %rax
        jbe     .Lprobe_tail
.Lprobe_loop:
        subq    $0x1000, %rcx
        testb   $0, (%rcx)
        subq    $0x1000, %rax
        cmpq    $0x1000, %rax
        ja      .Lprobe_loop
.Lprobe_tail:
        subq    %rax, %rcx
        testb   $0, (%rcx)
        popq    %rax
        popq    %rcx
        retq
