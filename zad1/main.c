#include "error.h"
#include "raise.h"

#include <stdlib.h>
#include <signal.h>
#include <ucontext.h>

#define STACK_SIZE 8192

static char stack[STACK_SIZE];
static ucontext_t context;

int main(int argc, char **argv) {
    CALL_NEQ(-1, getcontext(&context));
    context.uc_stack.ss_sp = stack;
    context.uc_stack.ss_size = sizeof(stack);
    context.uc_link = NULL;
    makecontext(&context, run, 2, argc, argv);
    CALL_NEQ(-1, setcontext(&context));
}
