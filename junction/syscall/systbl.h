// syscall.hpp - support for junction syscalls

#pragma once

#include <syscall.h>

#include "junction/kernel/ksys.h"
#include "junction/kernel/usys.h"

#define SYS_NR 456
#define SYSTBL_TRAMPOLINE_LOC (0x200000UL)

namespace junction {

extern "C" typedef uint64_t (*sysfn_t)(uint64_t, uint64_t, uint64_t, uint64_t,
                                       uint64_t, uint64_t);

// generated by systbl.py
extern "C" sysfn_t sys_tbl[SYS_NR];
extern sysfn_t sys_tbl_strace[SYS_NR];
extern const char *syscall_names[SYS_NR];
}  // namespace junction
