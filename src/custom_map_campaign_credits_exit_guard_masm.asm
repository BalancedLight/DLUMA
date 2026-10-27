; Exact DevTools animation-cleanup tail replacements.  Each normal path
; reproduces the game instructions exactly.  A null result is bypassed only
; while C++ has armed the verified custom-campaign teardown scope for this TID.
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

EXTERN g_dluma_campaign_credits_exit_guard_active:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_thread_id:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_suppressed_count:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_suppressed_1072e90:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_suppressed_10739b0:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_suppressed_1078a40:DWORD
EXTERN g_dluma_campaign_credits_exit_guard_resume_1072e90:QWORD
EXTERN g_dluma_campaign_credits_exit_guard_resume_10739b0:QWORD
EXTERN g_dluma_campaign_credits_exit_guard_resume_1074c13:QWORD
EXTERN g_dluma_campaign_credits_exit_guard_resume_1078a40:QWORD

.code

TAIL_GUARD MACRO proc_name:REQ, site_count:REQ, resume:REQ
PUBLIC proc_name
proc_name PROC
    test rax, rax
    jnz  original_tail
    cmp  dword ptr [g_dluma_campaign_credits_exit_guard_active], 1
    jne  original_tail
    mov  r8d, dword ptr gs:[48h]
    cmp  dword ptr [g_dluma_campaign_credits_exit_guard_thread_id], r8d
    jne  original_tail
    lock inc dword ptr [g_dluma_campaign_credits_exit_guard_suppressed_count]
    lock inc dword ptr [site_count]
    movaps xmmword ptr [rsp+20h], xmm0
    mov  dword ptr [rsp+20h], 0FFFFFFFFh
    jmp  qword ptr [resume]
original_tail:
    mov  r8, qword ptr [rax]
    mov  rcx, rax
    movaps xmmword ptr [rsp+20h], xmm0
    call qword ptr [r8+38h]
    mov  dword ptr [rsp+20h], 0FFFFFFFFh
    jmp  qword ptr [resume]
proc_name ENDP
ENDM

TAIL_GUARD dluma_campaign_credits_exit_tail_guard_1072e90_stub, \
    g_dluma_campaign_credits_exit_guard_suppressed_1072e90, \
    g_dluma_campaign_credits_exit_guard_resume_1072e90
TAIL_GUARD dluma_campaign_credits_exit_tail_guard_10739b0_stub, \
    g_dluma_campaign_credits_exit_guard_suppressed_10739b0, \
    g_dluma_campaign_credits_exit_guard_resume_10739b0
TAIL_GUARD dluma_campaign_credits_exit_tail_guard_1078a40_stub, \
    g_dluma_campaign_credits_exit_guard_suppressed_1078a40, \
    g_dluma_campaign_credits_exit_guard_resume_1078a40

; A vectored handler reaches this only after proving the unaligned 1074C13
; dereference fault.  The faulting mov has no side effect; replay its state
; cleanup and continue after the original immediate store.
PUBLIC dluma_campaign_credits_exit_unaligned_resume_stub
dluma_campaign_credits_exit_unaligned_resume_stub PROC
    movaps xmmword ptr [rsp+20h], xmm0
    mov  dword ptr [rsp+20h], 0FFFFFFFFh
    jmp  qword ptr [g_dluma_campaign_credits_exit_guard_resume_1074c13]
dluma_campaign_credits_exit_unaligned_resume_stub ENDP
END
