option casemap:none

EXTERN dluma_playerdi_ondestroy_observe_call:PROC

.const
ALIGN 16
playerdi_xmm_sentinel BYTE \
    010h,021h,032h,043h,054h,065h,076h,087h, \
    098h,0A9h,0BAh,0CBh,0DCh,0EDh,0FEh,00Fh
playerdi_destroy_time REAL4 1.25

.code
PUBLIC dluma_playerdi_abi_probe
dluma_playerdi_abi_probe PROC FRAME
    sub rsp,128h
    .allocstack 128h
    .endprolog

    mov [rsp+20h],rcx
    mov [rsp+28h],rdx
    mov [rsp+30h],rbx
    mov [rsp+38h],rbp
    mov [rsp+40h],rsi
    mov [rsp+48h],rdi
    mov [rsp+50h],r12
    mov [rsp+58h],r13
    mov [rsp+60h],r14
    mov [rsp+68h],r15
    movdqu [rsp+070h],xmm6
    movdqu [rsp+080h],xmm7
    movdqu [rsp+090h],xmm8
    movdqu [rsp+0A0h],xmm9
    movdqu [rsp+0B0h],xmm10
    movdqu [rsp+0C0h],xmm11
    movdqu [rsp+0D0h],xmm12
    movdqu [rsp+0E0h],xmm13
    movdqu [rsp+0F0h],xmm14
    movdqu [rsp+100h],xmm15

    mov rbx,01111222233334444h
    mov rbp,05555666677778888h
    mov rsi,09999AAAABBBBCCCCh
    mov rdi,0DDDDEEEEFFFF0001h
    mov r12,0123456789ABCDEF0h
    mov r13,00FEDCBA987654321h
    mov r14,013579BDF2468ACE0h
    mov r15,00ECA8642FDB97531h
    movdqu xmm6,xmmword ptr playerdi_xmm_sentinel
    movdqa xmm7,xmm6
    movdqa xmm8,xmm6
    movdqa xmm9,xmm6
    movdqa xmm10,xmm6
    movdqa xmm11,xmm6
    movdqa xmm12,xmm6
    movdqa xmm13,xmm6
    movdqa xmm14,xmm6
    movdqa xmm15,xmm6

    mov rcx,[rsp+20h]
    mov rdx,[rsp+28h]
    movss xmm2,playerdi_destroy_time
    mov [rsp+110h],rsp
    call dluma_playerdi_ondestroy_observe_call

    mov r11d,1
    cmp rsp,[rsp+110h]
    jne playerdi_abi_failed
    mov rax,01111222233334444h
    cmp rbx,rax
    jne playerdi_abi_failed
    mov rax,05555666677778888h
    cmp rbp,rax
    jne playerdi_abi_failed
    mov rax,09999AAAABBBBCCCCh
    cmp rsi,rax
    jne playerdi_abi_failed
    mov rax,0DDDDEEEEFFFF0001h
    cmp rdi,rax
    jne playerdi_abi_failed
    mov rax,0123456789ABCDEF0h
    cmp r12,rax
    jne playerdi_abi_failed
    mov rax,00FEDCBA987654321h
    cmp r13,rax
    jne playerdi_abi_failed
    mov rax,013579BDF2468ACE0h
    cmp r14,rax
    jne playerdi_abi_failed
    mov rax,00ECA8642FDB97531h
    cmp r15,rax
    jne playerdi_abi_failed

    movdqu xmm0,xmmword ptr playerdi_xmm_sentinel
    movdqa xmm1,xmm6
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm7
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm8
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm9
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm10
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm11
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm12
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm13
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm14
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    movdqa xmm1,xmm15
    pcmpeqb xmm1,xmm0
    pmovmskb eax,xmm1
    cmp eax,0FFFFh
    jne playerdi_abi_failed
    jmp playerdi_abi_restore

playerdi_abi_failed:
    xor r11d,r11d

playerdi_abi_restore:
    movdqu xmm6,[rsp+070h]
    movdqu xmm7,[rsp+080h]
    movdqu xmm8,[rsp+090h]
    movdqu xmm9,[rsp+0A0h]
    movdqu xmm10,[rsp+0B0h]
    movdqu xmm11,[rsp+0C0h]
    movdqu xmm12,[rsp+0D0h]
    movdqu xmm13,[rsp+0E0h]
    movdqu xmm14,[rsp+0F0h]
    movdqu xmm15,[rsp+100h]
    mov rbx,[rsp+30h]
    mov rbp,[rsp+38h]
    mov rsi,[rsp+40h]
    mov rdi,[rsp+48h]
    mov r12,[rsp+50h]
    mov r13,[rsp+58h]
    mov r14,[rsp+60h]
    mov r15,[rsp+68h]
    mov eax,r11d
    add rsp,128h
    ret
dluma_playerdi_abi_probe ENDP

END
