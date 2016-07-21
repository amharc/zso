#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "syscalls.h"

/** Stringification of the current line. Yay, cpp... */
#define STR(X) #X
#define STR2(X) STR(X)
#define STR_LINE STR2(__LINE__)

/** Perform a call and check if it returns retval. Exit otherwise */
#define CALL_EQ(retval, call)                                                 \
    if((retval) != (call)) {                                                  \
        perror(#call " failed in " __FILE__ ": " STR_LINE);                   \
        exit(EXIT_FAILURE);                                                   \
    }

/** Perform a call and check if does not return retval. Exit otherwise */
#define CALL_NEQ(retval, call)                                                \
    if((retval) == (call)) {                                                  \
        perror(#call " failed in "  __FILE__ ": " STR_LINE);                  \
        exit(EXIT_FAILURE);                                                   \
    }

/** Check if a condition holds and exit otherwise. Uses raw system calls,
 *  because it is used also after the point when neither glibc nor vdso
 *  should be trusted anymore
 */
#define EXPECT_MSG(condition)                                                 \
    #condition " failed at " __FILE__  ": " STR_LINE

#define EXPECT(condition)                                                     \
    if(!(condition)) {                                                        \
        raw_write(STDERR_FILENO, EXPECT_MSG(condition),                       \
                sizeof(EXPECT_MSG(condition)) - 1);                           \
        raw_exit(EXIT_FAILURE);                                               \
    }
