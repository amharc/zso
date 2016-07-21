#pragma once

#include <sys/user.h>

/** Restores registers from user_regs_struct and passes control to the restored process */
_Noreturn void pass_control(struct user_regs_struct *regs); 
