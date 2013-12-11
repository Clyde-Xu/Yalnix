#ifndef INT_HANDLER_H
#define INT_HANDLER_H

#include "hardware.h"
typedef void (*handler)(UserContext *);


// Trap Handlers
void trap_kernel_handler(UserContext *);
void trap_clock_handler(UserContext *);
void trap_illegal_handler(UserContext *);
void trap_memory_handler(UserContext *);
void trap_math_handler(UserContext *);
void trap_tty_rev_handler(UserContext *);
void trap_tty_trans_handler(UserContext *);
void trap_dummy_handler(UserContext *);


#endif
