option casemap:none
public NativeBirthCaller
public NativeBirthSite
.code
NativeBirthCaller proc frame
    sub rsp,28h
    .allocstack 28h
    .endprolog
    mov rax,[rcx]
    test r8b,r8b
    jnz short NativeBirthSite
    jmp short NativeBirthRuntime
    align 16
NativeBirthSite label near
    call qword ptr [rax+370h]
    jmp short NativeBirthAfter
NativeBirthRuntime label near
    call qword ptr [rax+368h]
NativeBirthAfter label near
    db 08bh,0cfh
    add rsp,28h
    ret
NativeBirthCaller endp
end
