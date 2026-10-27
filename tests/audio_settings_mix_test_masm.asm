OPTION CASEMAP:NONE
EXTERN dluma_audio_settings_dsp_mix_probe:PROC
PUBLIC dluma_test_audio_mix_call
.code
dluma_test_audio_mix_call PROC FRAME
    push r12
    .pushreg r12
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov r12, r8
    xor r8d, r8d
    call dluma_audio_settings_dsp_mix_probe
    add rsp, 20h
    pop r12
    ret
dluma_test_audio_mix_call ENDP
END
