; Exact Dying Light Player fs::full_path selected-source observation seam.
;
; The route at filesystem RVA 0x14FC0 is reached after the first successful
; CFSSource::FullPath call while the VFS shared lock is still held. RDI is the
; source-list cursor and [RDI-8] is the selected CFSSource*.
;
; This is an interior jump, so it must not call out or create another stack
; frame. It publishes into one fixed slot selected by the Windows x64 TEB
; thread id, executes all 18 stolen bytes inline, and jumps to RVA 0x14FD2.
;
; The unreachable 0x24-byte prefix reproduces fs::full_path's validated
; prologue solely to generate matching .pdata/.xdata. The public route begins
; after that prefix. Consequently an exception or stack walk in the seam
; unwinds the interrupted fs::full_path frame instead of treating this
; jump-entered code as a leaf function.
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

EXTERN g_dluma_audio_full_path_provider_probe_state:BYTE
EXTERN g_dluma_audio_full_path_provider_probe_active_calls:DWORD
EXTERN g_dluma_audio_full_path_provider_probe_resume:QWORD

DLUMA_AUDIO_PROBE_STATE_ACTIVE_SLOTS EQU 0000h
DLUMA_AUDIO_PROBE_STATE_SLOTS       EQU 0008h
DLUMA_AUDIO_PROBE_SLOT_THREAD_ID    EQU 0000h
DLUMA_AUDIO_PROBE_SLOT_ARMED        EQU 0004h
DLUMA_AUDIO_PROBE_SLOT_COUNT        EQU 000Ch
DLUMA_AUDIO_PROBE_SLOT_SOURCE       EQU 0018h
DLUMA_AUDIO_PROBE_SLOT_SHIFT        EQU 5
DLUMA_AUDIO_PROBE_SLOT_MASK         EQU 0FFh

.code
dluma_audio_full_path_provider_probe_unwind PROC FRAME
    ; Never executed. These are the exact fs::full_path 0x14D80 prologue
    ; bytes and unwind operations for the already-active engine frame.
    mov  qword ptr [rsp+008h], rbx
    mov  qword ptr [rsp+010h], rsi
    mov  qword ptr [rsp+018h], rdi
    mov  qword ptr [rsp+020h], r12
    push rbp
    .pushreg rbp
    lea  rbp, [rsp-0D0h]
    sub  rsp, 1D0h
    .allocstack 1D0h
    .savereg rbx, 1E0h
    .savereg rsi, 1E8h
    .savereg rdi, 1F0h
    .savereg r12, 1F8h
    .endprolog

PUBLIC dluma_audio_full_path_provider_probe
dluma_audio_full_path_provider_probe LABEL NEAR
    ; First stolen instruction consumes the incoming ZF before observation.
    cmovne r9, r8
    lock inc dword ptr [g_dluma_audio_full_path_provider_probe_active_calls]

    cmp  dword ptr [g_dluma_audio_full_path_provider_probe_state+DLUMA_AUDIO_PROBE_STATE_ACTIVE_SLOTS], 0
    jle  original_tail

    ; RDX is overwritten naturally at filesystem RVA 0x14FD5, so it is safe
    ; scratch storage for the source pointer. RAX/RCX/RDI are all overwritten
    ; by the remaining stolen instructions before the original resumes.
    mov  rdx, qword ptr [rdi-8]
    mov  eax, dword ptr gs:[48h]
    mov  ecx, eax
    and  eax, DLUMA_AUDIO_PROBE_SLOT_MASK
    shl  rax, DLUMA_AUDIO_PROBE_SLOT_SHIFT
    lea  rdi, g_dluma_audio_full_path_provider_probe_state
    add  rdi, DLUMA_AUDIO_PROBE_STATE_SLOTS
    add  rdi, rax
    cmp  dword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_THREAD_ID], ecx
    jne  original_tail
    cmp  dword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_ARMED], 1
    jne  original_tail
    cmp  dword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_COUNT], 2
    jae  original_tail
    cmp  dword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_COUNT], 0
    jne  increment_count
    mov  qword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_SOURCE], rdx
increment_count:
    inc  dword ptr [rdi+DLUMA_AUDIO_PROBE_SLOT_COUNT]

original_tail:
    lock dec dword ptr [g_dluma_audio_full_path_provider_probe_active_calls]
    ; Remaining 14 stolen bytes. OR overwrites every status flag used by the
    ; observation path, so the resumed machine state matches the original.
    or   rcx, -1
    xor  eax, eax
    mov  rdi, rbx
    repne scasb
    mov  rdi, r9
    jmp  qword ptr [g_dluma_audio_full_path_provider_probe_resume]
dluma_audio_full_path_provider_probe_unwind ENDP
END
