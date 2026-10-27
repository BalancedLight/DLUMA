; Minimal x64 stack probe for the no-CRT build.
; RAX contains the requested stack allocation size and must be preserved.
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

.code
PUBLIC __chkstk
__chkstk PROC
    push rcx
    push rax
    lea  rcx, [rsp + 24]
    cmp  rax, 1000h
    jbe  probe_tail
probe_loop:
    sub  rcx, 1000h
    test byte ptr [rcx], 0
    sub  rax, 1000h
    cmp  rax, 1000h
    ja   probe_loop
probe_tail:
    sub  rcx, rax
    test byte ptr [rcx], 0
    pop  rax
    pop  rcx
    ret
__chkstk ENDP
END
