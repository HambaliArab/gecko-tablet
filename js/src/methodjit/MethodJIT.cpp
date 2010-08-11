/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
 *
 * The Initial Developer of the Original Code is
 *   Brendan Eich <brendan@mozilla.org>
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "MethodJIT.h"
#include "Logging.h"
#include "assembler/jit/ExecutableAllocator.h"
#include "jstracer.h"
#include "BaseAssembler.h"
#include "MonoIC.h"
#include "PolyIC.h"
#include "TrampolineCompiler.h"

using namespace js;
using namespace js::mjit;

#ifdef JS_METHODJIT_PROFILE_STUBS
static const size_t STUB_CALLS_FOR_OP_COUNT = 255;
static uint32 StubCallsForOp[STUB_CALLS_FOR_OP_COUNT];
#endif

extern "C" void JS_FASTCALL
PushActiveVMFrame(VMFrame &f)
{
    f.previous = JS_METHODJIT_DATA(f.cx).activeFrame;
    JS_METHODJIT_DATA(f.cx).activeFrame = &f;
}

extern "C" void JS_FASTCALL
PopActiveVMFrame(VMFrame &f)
{
    JS_ASSERT(JS_METHODJIT_DATA(f.cx).activeFrame);
    JS_METHODJIT_DATA(f.cx).activeFrame = JS_METHODJIT_DATA(f.cx).activeFrame->previous;    
}

extern "C" void JS_FASTCALL
SetVMFrameRegs(VMFrame &f)
{
    f.oldRegs = f.cx->regs;
    f.cx->setCurrentRegs(&f.regs);
}

extern "C" void JS_FASTCALL
UnsetVMFrameRegs(VMFrame &f)
{
    *f.oldRegs = f.regs;
    f.cx->setCurrentRegs(f.oldRegs);
}

#if defined(__APPLE__) || defined(XP_WIN)
# define SYMBOL_STRING(name) "_" #name
#else
# define SYMBOL_STRING(name) #name
#endif

JS_STATIC_ASSERT(offsetof(JSFrameRegs, sp) == 0);

#if defined(__linux__) && defined(JS_CPU_X64)
# define SYMBOL_STRING_RELOC(name) #name "@plt"
#else
# define SYMBOL_STRING_RELOC(name) SYMBOL_STRING(name)
#endif

#if defined(XP_MACOSX)
# define HIDE_SYMBOL(name) ".private_extern _" #name
#elif defined(__linux__)
# define HIDE_SYMBOL(name) ".hidden" #name
#else
# define HIDE_SYMBOL(name)
#endif

#if defined(__GNUC__)

/* If this assert fails, you need to realign VMFrame to 16 bytes. */
#ifdef JS_CPU_ARM
JS_STATIC_ASSERT(sizeof(VMFrame) % 8 == 0);
#else
JS_STATIC_ASSERT(sizeof(VMFrame) % 16 == 0);
#endif

# if defined(JS_CPU_X64)

/*
 *    *** DANGER ***
 * If these assertions break, update the constants below.
 *    *** DANGER ***
 */
JS_STATIC_ASSERT(offsetof(VMFrame, savedRBX) == 0x58);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x40);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerTrampoline) "\n"
SYMBOL_STRING(JaegerTrampoline) ":"       "\n"
    /* Prologue. */
    "pushq %rbp"                         "\n"
    "movq %rsp, %rbp"                    "\n"
    /* Save non-volatile registers. */
    "pushq %r12"                         "\n"
    "pushq %r13"                         "\n"
    "pushq %r14"                         "\n"
    "pushq %r15"                         "\n"
    "pushq %rbx"                         "\n"

    /* Build the JIT frame.
     * rdi = cx
     * rsi = fp
     * rcx = inlineCallCount
     * fp must go into rbx
     */
    "pushq %rcx"                         "\n"
    "pushq %rdi"                         "\n"
    "pushq %rsi"                         "\n"
    "movq  %rsi, %rbx"                   "\n"

    /* Space for the rest of the VMFrame. */
    "subq  $0x38, %rsp"                  "\n"

    /* Set cx->regs and set the active frame (requires saving rdx). */
    "pushq %rdx"                         "\n"
    "movq  %rsp, %rdi"                   "\n"
    "call " SYMBOL_STRING_RELOC(SetVMFrameRegs) "\n"
    "movq  %rsp, %rdi"                   "\n"
    "call " SYMBOL_STRING_RELOC(PushActiveVMFrame) "\n"
    "popq  %rdx"                         "\n"

    /*
     * Jump into into the JIT'd code. The call implicitly fills in
     * the precious f.scriptedReturn member of VMFrame.
     */
    "call *%rdx"                         "\n"
    "leaq -8(%rsp), %rdi"                "\n"
    "call " SYMBOL_STRING_RELOC(PopActiveVMFrame) "\n"
    "leaq -8(%rsp), %rdi"                "\n"
    "call " SYMBOL_STRING_RELOC(UnsetVMFrameRegs) "\n"

    "addq $0x50, %rsp"                   "\n"
    "popq %rbx"                          "\n"
    "popq %r15"                          "\n"
    "popq %r14"                          "\n"
    "popq %r13"                          "\n"
    "popq %r12"                          "\n"
    "popq %rbp"                          "\n"
    "movq $1, %rax"                      "\n"
    "ret"                                "\n"
);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerThrowpoline)  "\n"
SYMBOL_STRING(JaegerThrowpoline) ":"        "\n"
    "movq %rsp, %rdi"                       "\n"
    "call " SYMBOL_STRING_RELOC(js_InternalThrow) "\n"
    "testq %rax, %rax"                      "\n"
    "je   throwpoline_exit"                 "\n"
    "jmp  *%rax"                            "\n"
  "throwpoline_exit:"                       "\n"
    "movq %rsp, %rdi"                       "\n"
    "call " SYMBOL_STRING_RELOC(PopActiveVMFrame) "\n"
    "addq $0x58, %rsp"                      "\n"
    "popq %rbx"                             "\n"
    "popq %r15"                             "\n"
    "popq %r14"                             "\n"
    "popq %r13"                             "\n"
    "popq %r12"                             "\n"
    "popq %rbp"                             "\n"
    "xorq %rax,%rax"                        "\n"
    "ret"                                   "\n"
);

JS_STATIC_ASSERT(offsetof(JSStackFrame, rval) == 0x40);
JS_STATIC_ASSERT(offsetof(JSStackFrame, ncode) == 0x60);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x40);

JS_STATIC_ASSERT(JSVAL_TAG_MASK == 0xFFFF800000000000LL);
JS_STATIC_ASSERT(JSVAL_PAYLOAD_MASK == 0x00007FFFFFFFFFFFLL);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerFromTracer)   "\n"
SYMBOL_STRING(JaegerFromTracer) ":"         "\n"
    "movq 0x40(%rbx), %rcx"                 "\n" /* fp->rval type (as value) */
    "movq $0xFFFF800000000000, %r11"         "\n" /* load type mask (JSVAL_TAG_MASK) */
    "andq %r11, %rcx"                       "\n" /* extract type */

    "movq 0x40(%rbx), %rdx"                 "\n" /* fp->rval type */
    "movq $0x00007FFFFFFFFFFF, %r11"        "\n" /* load payload mask (JSVAL_PAYLOAD_MASK) */
    "andq %r11, %rdx"                       "\n" /* extract payload */

    "movq 0x60(%rbx), %rax"                 "\n" /* fp->ncode */
    "movq 0x40(%rsp), %rbx"                 "\n" /* f.fp */
    "ret"                                   "\n"
);

# elif defined(JS_CPU_X86)

/*
 *    *** DANGER ***
 * If these assertions break, update the constants below. The throwpoline
 * should have the offset of savedEBX plus 4, because it needs to clean
 * up the argument.
 *    *** DANGER ***
 */
JS_STATIC_ASSERT(offsetof(VMFrame, savedEBX) == 0x2c);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x20);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerTrampoline) "\n"
SYMBOL_STRING(JaegerTrampoline) ":"       "\n"
    /* Prologue. */
    "pushl %ebp"                         "\n"
    "movl %esp, %ebp"                    "\n"
    /* Save non-volatile registers. */
    "pushl %esi"                         "\n"
    "pushl %edi"                         "\n"
    "pushl %ebx"                         "\n"

    /* Build the JIT frame. Push fields in order, 
     * then align the stack to form esp == VMFrame. */
    "pushl 20(%ebp)"                     "\n"
    "pushl 8(%ebp)"                      "\n"
    "pushl 12(%ebp)"                     "\n"
    "movl  12(%ebp), %ebx"               "\n"
    "subl $0x1c, %esp"                   "\n"

    /* Jump into the JIT'd code. */
    "pushl 16(%ebp)"                     "\n"
    "movl  %esp, %ecx"                   "\n"
    "call " SYMBOL_STRING_RELOC(SetVMFrameRegs) "\n"
    "movl  %esp, %ecx"                   "\n"
    "call " SYMBOL_STRING_RELOC(PushActiveVMFrame) "\n"
    "popl  %edx"                         "\n"

    "call  *%edx"                        "\n"
    "leal  -4(%esp), %ecx"               "\n"
    "call " SYMBOL_STRING_RELOC(PopActiveVMFrame) "\n"
    "leal  -4(%esp), %ecx"               "\n"
    "call " SYMBOL_STRING_RELOC(UnsetVMFrameRegs) "\n"

    "addl $0x28, %esp"                   "\n"
    "popl %ebx"                          "\n"
    "popl %edi"                          "\n"
    "popl %esi"                          "\n"
    "popl %ebp"                          "\n"
    "movl $1, %eax"                      "\n"
    "ret"                                "\n"
);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerThrowpoline)  "\n"
SYMBOL_STRING(JaegerThrowpoline) ":"        "\n"
    /* Align the stack to 16 bytes. */
    "pushl %esp"                         "\n"
    "pushl (%esp)"                       "\n"
    "pushl (%esp)"                       "\n"
    "pushl (%esp)"                       "\n"
    "call " SYMBOL_STRING_RELOC(js_InternalThrow) "\n"
    /* Bump the stack by 0x2c, as in the basic trampoline, but
     * also one more word to clean up the stack for js_InternalThrow,
     * and another to balance the alignment above. */
    "addl $0x10, %esp"                   "\n"
    "testl %eax, %eax"                   "\n"
    "je   throwpoline_exit"              "\n"
    "jmp  *%eax"                         "\n"
  "throwpoline_exit:"                    "\n"
    "movl %esp, %ecx"                    "\n"
    "call " SYMBOL_STRING_RELOC(PopActiveVMFrame) "\n"
    "addl $0x2c, %esp"                   "\n"
    "popl %ebx"                          "\n"
    "popl %edi"                          "\n"
    "popl %esi"                          "\n"
    "popl %ebp"                          "\n"
    "xorl %eax, %eax"                    "\n"
    "ret"                                "\n"
);

JS_STATIC_ASSERT(offsetof(JSStackFrame, rval) == 0x28);
JS_STATIC_ASSERT(offsetof(JSStackFrame, ncode) == 0x3C);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x20);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerFromTracer)   "\n"
SYMBOL_STRING(JaegerFromTracer) ":"         "\n"
    "movl 0x28(%ebx), %edx"                 "\n" /* fp->rval data */
    "movl 0x2C(%ebx), %ecx"                 "\n" /* fp->rval type */
    "movl 0x3C(%ebx), %eax"                 "\n" /* fp->ncode */
    "movl 0x20(%esp), %ebx"                 "\n" /* f.fp */
    "ret"                                   "\n"
);

# elif defined(JS_CPU_ARM)

JS_STATIC_ASSERT(offsetof(VMFrame, savedLR) == (sizeof(VMFrame)-4));
JS_STATIC_ASSERT(sizeof(VMFrame) == 80);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerFromTracer)   "\n"
SYMBOL_STRING(JaegerFromTracer) ":"         "\n"
    /* Restore frame regs. */
    "ldr r11, [sp, #32]"                    "\n"
    "bx  r0"                                "\n"
);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerTrampoline)   "\n"
SYMBOL_STRING(JaegerTrampoline) ":"         "\n"
    /*
     * On entry to JaegerTrampoline:
     *      r0 = cx
     *      r1 = fp
     *      r2 = code
     *      r3 = inlineCallCount
     *
     * The VMFrame for ARM looks like this:
     *  [ lr        ]   \
     *  [ r11       ]   |
     *  [ r10       ]   |
     *  [ r9        ]   | Callee-saved registers.                             
     *  [ r8        ]   | VFP registers d8-d15 may be required here too, but  
     *  [ r7        ]   | unconditionally preserving them might be expensive
     *  [ r6        ]   | considering that we might not use them anyway.
     *  [ r5        ]   |
     *  [ r4        ]   /
     *  [ ICallCnt  ]
     *  [ cx        ]
     *  [ fp        ]
     *  [ regs.sp   ]
     *  [ regs.pc   ]
     *  [ oldRegs   ]
     *  [ previous  ]
     *  [ args.ptr  ]
     *  [ args.ptr2 ]
     *  [ srpt. ret ]   } Scripted return.
     */
    
    /* Push callee-saved registers. TODO: Do we actually need to push all of them? If the
     * compiled JavaScript function is EABI-compliant, we only need to push what we use in
     * JaegerTrampoline. */
"   push    {r4-r11,lr}"                        "\n"
    /* Push interesting VMFrame content. */
"   push    {r0,r3}"                            "\n"    /* inlineCallCount, cx */
"   push    {r1}"                               "\n"    /* fp */
    /* Remaining fields are set elsewhere, but we need to leave space for them. */
"   sub     sp, sp, #(4*8)"                     "\n"

"   mov     r0, sp"                             "\n"
"   mov     r4, r2"                             "\n"    /* Preserve r2 ('code') in a callee-saved register. */
"   bl  " SYMBOL_STRING_RELOC(SetVMFrameRegs)   "\n"
"   mov     r0, sp"                             "\n"
"   bl  " SYMBOL_STRING_RELOC(PushActiveVMFrame)"\n"

    /* Call the compiled JavaScript function. We do this with an unaligned sp because the compiled
     * script explicitly pushes the return value into f->scriptedReturn. */
"   add     sp, sp, #(4*1)"                     "\n"
"   blx     r4"                                 "\n"
"   sub     sp, sp, #(4*1)"                     "\n"

    /* Tidy up. */
"   mov     r0, sp"                             "\n"
"   bl  " SYMBOL_STRING_RELOC(PopActiveVMFrame) "\n"
"   mov     r0, sp"                             "\n"
"   bl  " SYMBOL_STRING_RELOC(UnsetVMFrameRegs) "\n"

    /* Skip past the parameters we pushed (such as cx and the like). */
"   add     sp, sp, #(4*8 + 4*3)"               "\n"

    /* Set a 'true' return value to indicate successful completion. */
"   mov     r0, #1"                         "\n"
"   pop     {r4-r11,pc}"                    "\n"
);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerThrowpoline)  "\n"
SYMBOL_STRING(JaegerThrowpoline) ":"        "\n"
    /* Restore 'f', as it will have been clobbered. */
"   mov     r0, sp"                         "\n"

    /* Call the utility function that sets up the internal throw routine. */
"   bl  " SYMBOL_STRING_RELOC(js_InternalThrow) "\n"
    
    /* If 0 was returned, just bail out as normal. Otherwise, we have a 'catch' or 'finally' clause
     * to execute. */
"   cmp     r0, #0"                         "\n"
"   bxne    r0"                             "\n"

    /* Skip past the parameters we pushed (such as cx and the like). */
"   add     sp, sp, #(4*8 + 4*3)"               "\n"

"   pop     {r4-r11,pc}"                    "\n"
);

asm volatile (
".text\n"
".globl " SYMBOL_STRING(JaegerStubVeneer)   "\n"
SYMBOL_STRING(JaegerStubVeneer) ":"         "\n"
    /* We enter this function as a veneer between a compiled method and one of the js_ stubs. We
     * need to store the LR somewhere (so it can be modified in case on an exception) and then
     * branch to the js_ stub as if nothing had happened.
     * The arguments are identical to those for js_* except that the target function should be in
     * 'ip'. */
"   push    {ip,lr}"                        "\n"
"   blx     ip"                             "\n"
"   pop     {ip,pc}"                        "\n"
);

# else
#  error "Unsupported CPU!"
# endif
#elif defined(_MSC_VER)

#if defined(JS_CPU_X86)

/*
 *    *** DANGER ***
 * If these assertions break, update the constants below. The throwpoline
 * should have the offset of savedEBX plus 4, because it needs to clean
 * up the argument.
 *    *** DANGER ***
 */
JS_STATIC_ASSERT(offsetof(VMFrame, savedEBX) == 0x2c);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x20);

extern "C" {

    __declspec(naked) void JaegerFromTracer()
    {
        __asm {
            mov edx, [ebx + 0x28];
            mov ecx, [ebx + 0x2C];
            mov eax, [ebx + 0x3C];
            mov ebx, [esp + 0x20];
            ret;
        }
    }

    __declspec(naked) JSBool JaegerTrampoline(JSContext *cx, JSStackFrame *fp, void *code,
                                              uintptr_t inlineCallCount)
    {
        __asm {
            /* Prologue. */
            push ebp;
            mov ebp, esp;
            /* Save non-volatile registers. */
            push esi;
            push edi;
            push ebx;

            /* Build the JIT frame. Push fields in order, 
             * then align the stack to form esp == VMFrame. */
            push [ebp+20];
            push [ebp+8];
            push [ebp+12];
            mov  ebx, [ebp+12];
            sub  esp, 0x1c;

            /* Jump into into the JIT'd code. */
            push [ebp+16];
            mov  ecx, esp;
            call SetVMFrameRegs;
            mov  ecx, esp;
            call PushActiveVMFrame;
            pop  edx;

            call edx;
            lea  ecx, [esp-4];
            call PopActiveVMFrame;
            lea  ecx, [esp-4];
            call UnsetVMFrameRegs;

            add esp, 0x28

            pop ebx;
            pop edi;
            pop esi;
            pop ebp;
            mov eax, 1;
            ret;
        }
    }

    extern "C" void *js_InternalThrow(js::VMFrame &f);

    __declspec(naked) void *JaegerThrowpoline(js::VMFrame *vmFrame) {
        __asm {
            /* Align the stack to 16 bytes. */
            push esp;
            push [esp];
            push [esp];
            push [esp];
            call js_InternalThrow;
            /* Bump the stack by 0x2c, as in the basic trampoline, but
             * also one more word to clean up the stack for js_InternalThrow,
             * and another to balance the alignment above. */
            add esp, 0x10;
            test eax, eax;
            je throwpoline_exit;
            jmp eax;
        throwpoline_exit:
            mov ecx, esp;
            call PopActiveVMFrame;
            add esp, 0x2c;
            pop ebx;
            pop edi;
            pop esi;
            pop ebp;
            xor eax, eax
            ret;
        }
    }
}

#elif defined(JS_CPU_X64)

/*
 *    *** DANGER ***
 * If these assertions break, update the constants below.
 *    *** DANGER ***
 */
JS_STATIC_ASSERT(offsetof(VMFrame, savedRBX) == 0x48);
JS_STATIC_ASSERT(offsetof(VMFrame, fp) == 0x30);

// Windows x64 uses assembler version since compiler doesn't support
// inline assembler
#else
#  error "Unsupported CPU!"
#endif

#endif                   /* _MSC_VER */

bool
ThreadData::Initialize()
{
    execPool = new JSC::ExecutableAllocator();
    if (!execPool)
        return false;
    
    TrampolineCompiler tc(execPool, &trampolines);
    if (!tc.compile()) {
        delete execPool;
        return false;
    }

    if (!picScripts.init()) {
        delete execPool;
        return false;
    }

#ifdef JS_METHODJIT_PROFILE_STUBS
    for (size_t i = 0; i < STUB_CALLS_FOR_OP_COUNT; ++i)
        StubCallsForOp[i] = 0;
#endif

    activeFrame = NULL;

    return true;
}

void
ThreadData::Finish()
{
    TrampolineCompiler::release(&trampolines);
    delete execPool;
#ifdef JS_METHODJIT_PROFILE_STUBS
    FILE *fp = fopen("/tmp/stub-profiling", "wt");
# define OPDEF(op,val,name,image,length,nuses,ndefs,prec,format) \
    fprintf(fp, "%03d %s %d\n", val, #op, StubCallsForOp[val]);
# include "jsopcode.tbl"
# undef OPDEF
    fclose(fp);
#endif
}

bool
ThreadData::addScript(JSScript *script)
{
    ScriptSet::AddPtr p = picScripts.lookupForAdd(script);
    if (p)
        return true;
    return picScripts.add(p, script);
}

void
ThreadData::removeScript(JSScript *script)
{
    ScriptSet::Ptr p = picScripts.lookup(script);
    if (p)
        picScripts.remove(p);
}

void
ThreadData::purge(JSContext *cx)
{
    if (!cx->runtime->gcRegenShapes)
        return;

    for (ThreadData::ScriptSet::Enum e(picScripts); !e.empty(); e.popFront()) {
#if defined JS_POLYIC
        JSScript *script = e.front();
        ic::PurgePICs(cx, script);
#endif
#if defined JS_MONOIC
        //PurgeMICs(cs, script);
#endif
    }

    picScripts.clear();
}


extern "C" JSBool JaegerTrampoline(JSContext *cx, JSStackFrame *fp, void *code,
                                   uintptr_t inlineCallCount);

JSBool
mjit::JaegerShot(JSContext *cx)
{
    JS_ASSERT(cx->regs);

    JS_CHECK_RECURSION(cx, return JS_FALSE;);

    void *code;
    jsbytecode *pc = cx->regs->pc;
    JSStackFrame *fp = cx->fp;
    JSScript *script = fp->script;
    uintptr_t inlineCallCount = 0;

    JS_ASSERT(script->ncode && script->ncode != JS_UNJITTABLE_METHOD);

#ifdef JS_TRACER
    if (TRACE_RECORDER(cx))
        AbortRecording(cx, "attempt to enter method JIT while recording");
#endif

    if (pc == script->code)
        code = script->nmap[-1];
    else
        code = script->nmap[pc - script->code];

    JS_ASSERT(code);

#ifdef JS_METHODJIT_SPEW
    Profiler prof;

    JaegerSpew(JSpew_Prof, "entering jaeger script: %s, line %d\n", fp->script->filename,
               fp->script->lineno);
    prof.start();
#endif

#ifdef DEBUG
    JSStackFrame *checkFp = fp;
#endif
#if 0
    uintptr_t iCC = inlineCallCount;
    while (iCC--)
        checkFp = checkFp->down;
#endif

    JSAutoResolveFlags rf(cx, JSRESOLVE_INFER);
    JSBool ok = JaegerTrampoline(cx, fp, code, inlineCallCount);

    JS_ASSERT(checkFp == cx->fp);

#ifdef JS_METHODJIT_SPEW
    prof.stop();
    JaegerSpew(JSpew_Prof, "script run took %d ms\n", prof.time_ms());
#endif

    return ok;
}

template <typename T>
static inline void Destroy(T &t)
{
    t.~T();
}

void
mjit::ReleaseScriptCode(JSContext *cx, JSScript *script)
{
    if (script->execPool) {
#if defined DEBUG && (defined JS_CPU_X86 || defined JS_CPU_X64) 
        memset(script->nmap[-1], 0xcc, script->inlineLength + script->outOfLineLength);
#endif
        script->execPool->release();
        script->execPool = NULL;
        // Releasing the execPool takes care of releasing the code.
        script->ncode = NULL;
        script->inlineLength = 0;
        script->outOfLineLength = 0;
        
#if defined JS_POLYIC
        if (script->pics) {
            uint32 npics = script->numPICs();
            for (uint32 i = 0; i < npics; i++) {
                script->pics[i].releasePools();
                Destroy(script->pics[i].execPools);
            }
            JS_METHODJIT_DATA(cx).removeScript(script);
            cx->free((uint8*)script->pics - sizeof(uint32));
        }
#endif
    }

    if (script->nmap) {
        cx->free(script->nmap - 1);
        script->nmap = NULL;
    }
    if (script->callSites) {
        cx->free(script->callSites - 1);
        script->callSites = NULL;
    }
#if defined JS_MONOIC
    if (script->mics) {
        cx->free(script->mics);
        script->mics = NULL;
    }
#endif

# if 0 /* def JS_TRACER */
    if (script->trees) {
        cx->free(script->trees);
        script->trees = NULL;
    }
# endif
}

#ifdef JS_METHODJIT_PROFILE_STUBS
void JS_FASTCALL
mjit::ProfileStubCall(VMFrame &f)
{
    JSOp op = JSOp(*f.regs.pc);
    StubCallsForOp[op]++;
}
#endif

