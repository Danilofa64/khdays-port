/* NitroSDK thread contexts (libs/nitro/os/asm_stubs: OS_InitContext,
 * OS_SaveContext, OS_LoadContext): the register images the SDK reads and
 * writes, on the port's execution contexts (runtime/core/threads.c, which keeps
 * which context runs each image).
 *
 * The SDK's scheduler is decompiled C and runs unchanged; only the register
 * save/restore underneath is native (runtime/core/threads.h has the idiom). */
#include "hal.h"
#include "os_context.h"
#include "../runtime/core/threads.h"

extern void CP_SaveContext(void *pContext);
extern void CPi_RestoreContext(const void *pContext);

/* The assembly's register image, kept for code that reads it; the execution
 * context is what actually runs. */
void OS_InitContext(OSContext *context, u32 newpc, u32 newsp)
{
    newpc += 4;
    context->pc_plus4 = newpc;
    context->sp_svc = newsp;
    newsp -= 0x40;  /* HW_SVC_STACK_SIZE */
    if (newsp & 4) {
        newsp -= 4;
    }
    context->sp = newsp;
    context->cpsr = (newpc & 1) ? 0x3f : 0x1f;  /* SYS mode, Thumb or ARM */
    for (int i = 0; i < 13; ++i) {
        context->r[i] = 0;
    }
    context->lr = 0;
    if (khdays_thread_tracing()) {
        khdays_thread_trace("init", (u32)context);
    }
    khdays_thread_start((u32)context);
}

int OS_SaveContext(OSContext *context)
{
    khdays_thread_adopt((u32)context);
    CP_SaveContext(context->cp_context);
    context->cpsr = khdays_cpsr;
    return 0;
}

void OS_LoadContext(OSContext *context)
{
    if (khdays_thread_tracing()) {
        khdays_thread_trace("switch to", (u32)context);
    }
    CPi_RestoreContext(context->cp_context);
    /* The thread resumes with its own mode and interrupt state (the assembly
     * returns through spsr = the saved cpsr). */
    khdays_cpsr = context->cpsr;
    khdays_thread_switch((u32)context);
}
