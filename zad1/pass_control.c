#include "pass_control.h"

/** The registers to be restored are firstly copied to global
 *  static variables (declared by the DECL_REG macro).
 *
 *  Then all registers are restored from those global variables
 *  and a memory-indirect jump is performed
 */

#define DECL_REG(name) \
    static long name asm(#name) __attribute__((used));

#define COPY_REG(name) \
    name = regs->name;

#define ASM_REG(name) \
    "movl " #name ", %%" #name "\n"

#define FOREACH_GP(MACRO) \
    MACRO(eax) \
    MACRO(ecx) \
    MACRO(edx) \
    MACRO(ebx) \
    MACRO(esp) \
    MACRO(ebp) \
    MACRO(esi) \
    MACRO(edi)

#define FOREACH(MACRO) \
    FOREACH_GP(MACRO) \
    MACRO(eflags) \
    MACRO(eip)

FOREACH(DECL_REG)
DECL_REG(xgs)

_Noreturn void pass_control(struct user_regs_struct *regs) {
    FOREACH(COPY_REG);
    COPY_REG(xgs);
    asm volatile(
        "pushl eflags\n"
        "popf\n"
        FOREACH_GP(ASM_REG)
        "movw xgs, %%gs\n"  // load the TLS 
        "jmpl *eip\n"
        ::: "memory", "cc"
    );
    __builtin_unreachable();
}
