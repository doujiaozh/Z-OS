#ifndef Z_OS_USER_H
#define Z_OS_USER_H

#include "types.h"

struct address_space;

extern u8 user_program_start[];
extern u8 user_program_end[];

int user_run_demo(u64 *exit_code);
int user_run_in_address_space(struct address_space *space, u64 entry, u64 user_stack, u64 *exit_code);
void user_finish(u64 status) __attribute__((noreturn));
bool user_handle_exception(u64 vector);

#endif
