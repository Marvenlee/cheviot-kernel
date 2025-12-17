#ifndef KERNEL_DBG_H
#define KERNEL_DBG_H

#include <kernel/arch.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <stdarg.h>
#include <stdint.h>


#define LOG_LEVEL_ERROR   0
#define LOG_LEVEL_WARN    1
#define LOG_LEVEL_INFO    2
#define LOG_LEVEL_DEBUG   3

#include <kernel/dbg_config.h>

#ifdef NDEBUG
#undef KDEBUG
#endif

#ifdef KDEBUG

#define KLOG_REGISTER(debug_level)                    \
  static int __attribute__((unused)) _file_debug_level = debug_level;

#define klog_error(fmt, args...) { if (_file_debug_level >= LOG_LEVEL_ERROR) DoLog(fmt, ##args); }
#define klog_warn(fmt, args...) { if (_file_debug_level >= LOG_LEVEL_WARN) DoLog(fmt, ##args); }
#define klog_info(fmt, args...) { if (_file_debug_level >= LOG_LEVEL_INFO) DoLog(fmt, ##args); }
#define klog_debug(fmt, args...) { if (_file_debug_level >= LOG_LEVEL_DEBUG) DoLog(fmt, ##args); }

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
int sys_debug_dump(int cmd, uint32_t arg1, uint32_t arg2);
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
