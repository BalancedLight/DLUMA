OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE
.code
PUBLIC file_lifetime_test_probe
file_lifetime_test_probe PROC FRAME
 push rbx
 .pushreg rbx
 push rdi
 .pushreg rdi
 sub rsp,28h
 .allocstack 28h
 .endprolog
 mov rbx,rcx
 mov rdi,rcx
 call rdx
 add rsp,28h
 pop rdi
 pop rbx
 ret
file_lifetime_test_probe ENDP
PUBLIC file_lifetime_test_return
file_lifetime_test_return PROC
 ret
file_lifetime_test_return ENDP
END
