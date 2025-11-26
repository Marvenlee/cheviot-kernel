#ifndef KERNEL_DBG_H
#define KERNEL_DBG_H

#include <kernel/arch.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <stdarg.h>
#include <stdint.h>

/*
 * At end of most debug statements semi-colons aren't needed or will
 * cause problems compiling.
 *
 * Multi-statement Debug macros have braces surrounding them in case
 * they're used like below...
 *
 * if (condition)
 *     KPANIC (str);
 *
 *
 * Use -DNDEBUG in the makefile CFLAGS to remove debugging.
 * Use -DDEBUG_LEVEL=x to set debugging level.
 * Or define DEBUG_LEVEL at top of source file.
 */

//#define NDEBUG

#ifdef NDEBUG
#undef KDEBUG
#endif

#ifdef KDEBUG

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 2
#endif

#define Error(fmt, args...) DoLog(fmt, ##args)

#if DEBUG_LEVEL >= 1
#define Warn(fmt, args...) DoLog(fmt, ##args)
#else
#define Warn(fmt, args...)
#endif

#if DEBUG_LEVEL >= 2
#define Info(fmt, args...) DoLog(fmt, ##args)
#else
#define Info(fmt, args...)
#endif

#if DEBUG_LEVEL >= 3
#define KDebug(fmt, args...) DoLog(fmt, ##args)
#else
#define KDebug(fmt, args...)
#endif

#define KASSERT(expr)                                                          \
  {                                                                            \
    if (!(expr)) {                                                             \
      PrintKernelPanic(#expr ", %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);\
    }                                                                          \
  }


#define KernelPanic()                                                          \
  {                                                                            \
    DisableInterrupts();                                                       \
    PrintKernelPanic("panic, %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);   \
  }


#else

#define Error(fmt, args...)
#define Warn(fmt, args...)
#define Info(fmt, args...)
#define KDebug(fmt, args...)

#define KASSERT(expr)                                                          \
  {                                                                            \
    if (!(expr)) {                                                             \
      PrintKernelPanic(#expr ", %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);\
    }                                                                          \
  }

#define KernelPanic()                                                          \
  {                                                                            \
    DisableInterrupts();                                                       \
    PrintKernelPanic("panic, %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);   \
    while(1);                                                                  \
  }

#endif


/*
 * Kernel debugging functions.
 */

// debug/debug.c
void SysDebug(char *s);
void DoLog(const char *format, ...);
void KLog2(const char *format, va_list ap);
void PrintKernelPanic(char *format, ...);
void KLogToScreenDisable(void);
void NotifyLoggerProcessesInitialized(void);
void PrintUserContext(void *user_context);
void PrintMemDump(uint32_t base, size_t word_cnt);

// debug/dumpkerneltables.c
int sys_dumpkerneltables(int cmd, uint32_t arg1, uint32_t arg2);
void dump_kernel_processes(int cmd, int arg1, int arg2);
void dump_kernel_filps(int cmd, int arg1, int arg2);
void dump_kernel_vnodes(int cmd, int arg1, int arg2);
void dump_kernel_superblocks(int cmd, int arg1, int arg2);
void dump_kernel_kqueues(int cmd, int arg1, int arg2);
void dump_kernel_pipes(int cmd, int arg1, int arg2);

// boards/<board>/debug.c
void arch_debug_init(void);
void arch_debug_print_char(char ch);
void arch_debug_print_user_context(void *user_context);
void arch_kernel_panic(void);

#endif
