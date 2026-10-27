; Exact Player filesystem IOQueue final-reference-release observations.
; No callback, native call, allocation, lock wait or additional stack frame.
; RAX/RCX/RDX are dead at all three verified sites: they are overwritten by
; the following native CloseHandle/LeaveCriticalSection or next wait loop.
; The following JNZ uses ZF produced by native LOCK DEC in the relay. The
; remaining native paths overwrite the other flags before any subsequent use
; (DEC itself deliberately does not change CF).
OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE
EXTERN g_dluma_file_completion_ring:QWORD
EXTERN g_dluma_file_completion_probe_active_calls:DWORD
EXTERN g_dluma_file_completion_cancel_relay:QWORD
EXTERN g_dluma_file_completion_skipped_relay:QWORD
EXTERN g_dluma_file_completion_read_relay:QWORD
.code

PUBLISH_COMPLETION MACRO control:REQ, relay:REQ
 LOCAL original, overflow
 lock inc dword ptr [g_dluma_file_completion_probe_active_calls]
 mov rcx, qword ptr [g_dluma_file_completion_ring]
 test rcx, rcx
 jz original
 mov eax, 1
 lock xadd qword ptr [rcx], rax
 test rax, rax
 js overflow
 mov rdx, rax
 sub rdx, qword ptr [rcx+8]
 cmp rdx, 8192
 jae overflow
 mov rdx, rax
 and edx, 8191
 shl rdx, 4
 lea rcx, [rcx+rdx+32]
 mov qword ptr [rcx], control
 inc rax
 mov qword ptr [rcx+8], rax
 jmp original
overflow:
 mov dword ptr [rcx+16], 1
original:
 lock dec dword ptr [g_dluma_file_completion_probe_active_calls]
 jmp qword ptr [relay]
ENDM

; Unreachable prologue describes IOQueue::cancel's already active frame.
dluma_file_completion_cancel_unwind PROC FRAME
 db 04Dh,085h,0C0h
 db 00Fh,084h,042h,001h,000h,000h
 mov qword ptr [rsp+10h], rbx
 push rdi
 .pushreg rdi
 sub rsp, 20h
 .allocstack 20h
 .savereg rbx,38h
 ; The exact hot/chained cancellation range additionally preserves RSI.
 mov qword ptr [rsp+30h],rsi
 .savereg rsi,30h
 .endprolog
 PUBLIC dluma_file_completion_cancel_probe
dluma_file_completion_cancel_probe LABEL NEAR
 PUBLISH_COMPLETION rbx,g_dluma_file_completion_cancel_relay
dluma_file_completion_cancel_unwind ENDP

WORKER_PROBE MACRO unwind_name:REQ, entry_name:REQ, relay:REQ
unwind_name PROC FRAME
 ; Unreachable copy of IOQueue::runtime's validated native frame allocation.
 mov qword ptr [rsp], rcx
 push rbp
 .pushreg rbp
 push rbx
 .pushreg rbx
 push rsi
 .pushreg rsi
 push rdi
 .pushreg rdi
 push r12
 .pushreg r12
 push r13
 .pushreg r13
 push r14
 .pushreg r14
 push r15
 .pushreg r15
 lea rbp,[rsp-128h]
 sub rsp,228h
 .allocstack 228h
 .endprolog
 PUBLIC entry_name
entry_name LABEL NEAR
 PUBLISH_COMPLETION rdi,relay
unwind_name ENDP
ENDM
WORKER_PROBE dluma_file_completion_skipped_unwind,dluma_file_completion_skipped_probe,g_dluma_file_completion_skipped_relay
WORKER_PROBE dluma_file_completion_read_unwind,dluma_file_completion_read_probe,g_dluma_file_completion_read_relay
END
