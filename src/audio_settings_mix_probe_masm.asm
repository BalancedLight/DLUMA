; The exact Player worker's DSP-input loop keeps its bus index in R12.
; Preserve its caller, stack, nonvolatile registers and native float argument.
OPTION CASEMAP:NONE
EXTERN dluma_audio_settings_dsp_mix_hook:PROC
PUBLIC dluma_audio_settings_dsp_mix_probe
.code
dluma_audio_settings_dsp_mix_probe PROC
    mov r8, r12
    jmp dluma_audio_settings_dsp_mix_hook
dluma_audio_settings_dsp_mix_probe ENDP
END
