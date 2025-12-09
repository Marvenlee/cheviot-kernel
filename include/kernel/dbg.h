#ifndef KERNEL_DBG_H
#define KERNEL_DBG_H

#include <kernel/arch.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <stdarg.h>
#include <stdint.h>

//#define NDEBUG

#ifdef NDEBUG
#undef KDEBUG
#endif

#ifdef KDEBUG

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 2
#endif

#define klog_error(fmt, args...) DoLog(fmt, ##args)

#if DEBUG_LEVEL >= 1
#define klog_warn(fmt, args...) DoLog(fmt, ##args)
#else
#define klog_warn(fmt, args...)
#endif

#if DEBUG_LEVEL >= 2
#define klog_info(fmt, args...) DoLog(fmt, ##args)
#else
#define klog_info(fmt, args...)
#endif

#if DEBUG_LEVEL >= 3
#define klog_debug(fmt, args...) DoLog(fmt, ##args)
#else
#define klog_debug(fmt, args...)
#endif



#else

#define klog_error(fmt, args...)
#define klog_warn(fmt, args...)
#define klog_info(fmt, args...)
#define klog_debug(fmt, args...)

#endif

#define kassert(expr)                                                          \
  {                                                                            \
    if (!(expr)) {                                                             \
      do_kernelpanic(#expr ", %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);  \
    }                                                                          \
  }


#define kernelpanic()                                                             \
  {                                                                               \
    do_kernelpanic("kernelpanic: %s, %d, %s", __FILE__, __LINE__, __FUNCTION__);  \
  }


/*
 * Kernel debugging functions.
 */

// debug/debug.c
void SysDebug(char *s);
void DoLog(const char *format, ...);
void KLog2(const char *format, va_list ap);
void do_kernelpanic(char *format, ...);
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
void dump_kernel_pipes(int cmd, int arg1, int arg2);

// boards/<board>/debug.c
void arch_debug_init(void);
void arch_debug_print_char(char ch);
void arch_debug_print_user_context(void *user_context);
void arch_kernel_panic(void);

#endif
