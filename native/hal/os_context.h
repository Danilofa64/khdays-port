/* NitroSDK's OSContext, the register image a thread's context keeps: cpsr,
 * r0-r12, sp, lr, pc+4, the SVC sp, then the CP (divider) context at +0x48.
 * Words only (no pointer), so the same on the game's side (hal/sdk_thread.c)
 * and the port's (runtime/core/threads.c). */
#ifndef KHDAYS_OS_CONTEXT_H
#define KHDAYS_OS_CONTEXT_H

typedef struct OSContext {
    unsigned int cpsr;
    unsigned int r[13];
    unsigned int sp;
    unsigned int lr;
    unsigned int pc_plus4;
    unsigned int sp_svc;
    unsigned char cp_context[0x1c];
} OSContext;

#endif
