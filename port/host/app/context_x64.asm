; The switch between execution contexts on 64-bit Windows (context_win.c):
; the registers the Windows x64 calling convention keeps across a call --
; rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15, MXCSR's control bits and the x87
; control word -- on the context's own stack, the stack pointer kept in the
; context, and the thread's stack as Windows knows it -- the stack's base
; and limit and its allocation base in the thread's environment block --
; switched with it, as a fiber's are.

.code

; void khdays_context_jump(void **save_sp, void *load_sp)
khdays_context_jump PROC
    push rbp
    push rbx
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 168                  ; xmm6-xmm15, then MXCSR and the x87 control word
    movaps [rsp + 0], xmm6
    movaps [rsp + 16], xmm7
    movaps [rsp + 32], xmm8
    movaps [rsp + 48], xmm9
    movaps [rsp + 64], xmm10
    movaps [rsp + 80], xmm11
    movaps [rsp + 96], xmm12
    movaps [rsp + 112], xmm13
    movaps [rsp + 128], xmm14
    movaps [rsp + 144], xmm15
    stmxcsr dword ptr [rsp + 160]
    fnstcw word ptr [rsp + 164]
    push qword ptr gs:[8]         ; StackBase
    push qword ptr gs:[16]        ; StackLimit
    push qword ptr gs:[1478h]     ; DeallocationStack
    mov [rcx], rsp
    mov rsp, rdx
    pop qword ptr gs:[1478h]
    pop qword ptr gs:[16]
    pop qword ptr gs:[8]
    movaps xmm6, [rsp + 0]
    movaps xmm7, [rsp + 16]
    movaps xmm8, [rsp + 32]
    movaps xmm9, [rsp + 48]
    movaps xmm10, [rsp + 64]
    movaps xmm11, [rsp + 80]
    movaps xmm12, [rsp + 96]
    movaps xmm13, [rsp + 112]
    movaps xmm14, [rsp + 128]
    movaps xmm15, [rsp + 144]
    ldmxcsr dword ptr [rsp + 160]
    fldcw word ptr [rsp + 164]
    add rsp, 168
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbx
    pop rbp
    ret
khdays_context_jump ENDP

EXTERN khdays_context_started:PROC

; A new context's first return lands here, its KhdaysContext in rbx; the
; stack 16-byte aligned, as after a call returns (context_win.c's frame).
khdays_context_trampoline PROC
    mov rcx, rbx
    sub rsp, 32                   ; the callee's home area, the stack still aligned
    call khdays_context_started
    int 3                         ; khdays_context_started does not return
khdays_context_trampoline ENDP

END
