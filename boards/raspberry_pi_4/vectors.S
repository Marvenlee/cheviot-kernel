/*
 * Copyright 2014  Marven Gilhespie
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <kernel/board/arm.i>
#include <kernel/board/task.i>
#include <kernel/board/macros.i>

.global swi_vector
.global prefetch_abort_vector
.global data_abort_vector
.global reserved_vector
.global irq_vector
.global reset_vector
.global undef_instr_vector
.global fiq_vector
.global start_of_forked_process
.global start_of_execed_process
.global start_of_user_thread
.global start_of_kernel_thread

.section .text

/* @brief    System call table
 *
 * System call table of the kernel functions exposed to user mode through the swi
 * trap / software interrupt mechanism.
 * 
 * The call flow from app to kernel function is:
 *
 * app -> (libc) write -> _swi_write -> (kernel mode) swi_vector -> sys_write -> ...
 *
 * Newlib is used as the C standard Library. See the following newlib file:
 * third_party/newlib-4.1.0/newlib/libc/sys/arm/syscall.S for the corresponding
 * _swi_xxxx functions.
 */
.balign 16
syscall_table:
    .long sys_unknownsyscallhandler     // 0
    .long sys_debug                     // 1

    .long sys_fork	                    // 2  
    .long sys_exit                      // 3
    .long sys_waitpid                   // 4
    .long sys_kill                      // 5
    .long sys_setschedparams            // 6 

    .long sys_sleep                     // 7   
    .long sys_alarm                     // 8
    .long sys_nanosleep                 // 9
    .long sys_gettimeofday              // 10
    .long sys_settimeofday              // 11
    
    .long sys_virtualalloc              // 12   TODO: Replace with mmap
    .long sys_virtualallocphys          // 13   TODO: Replace with mmap
    .long sys_virtualfree               // 14   TODO: Replace with munmap
    .long sys_virtualprotect            // 15   TODO: Replace with mprotect

    .long sys_addinterruptserver        // 16
    .long sys_maskinterrupt             // 17
    .long sys_unmaskinterrupt           // 18

    .long sys_deprecatedsyscall         // 19   TODO: Was set_syslog_file, replaced with ioctl
                                                // TODO: Add open_syslog() to get syslog handle for writing.
    .long sys_exec                      // 20   

    // Add sys_mountport,  sys_createport can be an atomic/combined mknod2() and mountport();     
    .long sys_createmsgport             // 21   TODO: Rename to createport(). 
    .long sys_deprecatedsyscall         // 22   TODO: Was unmount, add back again, unmount(path, force)

    // Replace with just Read/Write calls. No seek.  Write header then immediately the data.
    // Need listen(mount_fd) to create new listener handle (multiple for char device server)
    
    .long sys_sendmsg                   // 23
    .long sys_getmsg                    // 24  
    .long sys_replymsg                  // 25
    .long sys_readmsg                   // 26
    .long sys_writemsg                  // 27
        
    .long sys_open                      // 28   
    .long sys_close                     // 29
    .long sys_dup                       // 30
    .long sys_dup2                      // 31
    
    .long sys_read                      // 32
    .long sys_write                     // 33
    .long sys_lseek                     // 34  FIXME: Why 2 syscalls for Seek ?
    .long sys_lseek64                   // 35
    
    .long sys_truncate                  // 36
    .long sys_unlink                    // 37

    .long sys_createdir                 // 38
    .long sys_opendir                   // 39
    .long sys_readdir                   // 40
    .long sys_rewinddir                 // 41
    .long sys_rmdir                     // 42

    .long sys_rename                    // 43

    .long sys_pipe                      // 44 Pipe
    .long sys_deprecatedsyscall         // 45 TODO: SocketPair (or generic socket creation call)

    .long sys_chdir                     // 46
    .long sys_fchdir                    // 47       

    .long sys_stat                      // 48
    .long sys_fstat                     // 49

    .long sys_symlink                   // 50
    .long sys_readlink                  // 51

    .long sys_chmod                     // 52
    .long sys_chown                     // 53
    .long sys_access                    // 54
    .long sys_umask                     // 55

    .long sys_getpid                    // 56
    .long sys_getppid                   // 57
    .long sys_getuid                    // 58
    .long sys_getgid                    // 59
    .long sys_geteuid                   // 60
    .long sys_getegid                   // 61
    
    .long sys_setuid                    // 62
    .long sys_setgid                    // 63
    .long sys_setpgrp                   // 64    
    .long sys_getpgrp                   // 65
    
    .long sys_virtualtophysaddr         // 66
    
    .long sys_signalnotify              // 67 
    .long sys_knotei                    // 68
    
    .long sys_pivotroot                 // 69

    .long sys_fcntl                     // 70  
    .long sys_isatty                    // 71  
    .long sys_ioctl                     // 72    
    .long sys_sync                      // 73
    .long sys_fsync                     // 74

    .long sys_sigaction                 // 75
    .long sys_sigprocmask               // 76
    .long sys_sigpending                // 77
    .long sys_sigsuspend                // 78
    
    .long sys_mknod                     // 79
    .long sys_renamemsgport             // 80       // TODO: Change to renameport()
    
    .long sys_chroot                    // 81

    .long sys_kqueue                    // 82 
    .long sys_kevent                    // 83

		.long sys_setegid										// 84
		.long sys_seteuid										// 85
		.long sys_issetugid									// 86
		.long sys_setgroups									// 87
		.long sys_getgroups									// 88
		.long sys_getpriority	  						// 89
		.long sys_setpriority								// 90
		.long sys_deprecatedsyscall					// 91  FIXME: Was sys_sysconf
		.long sys_fchmod										// 92
		.long sys_fchown										// 93

		.long sys_clock_gettime							// 94

    // TODO: Move into pi_mailbox server initially, use sendrec API
    // then move into device manager ring/ref monitor,

		.long sys_deprecatedsyscall					// 95   // FIXME: Architecture-specific syscalls, to be
		.long sys_deprecatedsyscall				  // 96   // moved out of kernel.
		.long sys_deprecatedsyscall		  		// 97   // for gpio and mailbox access.

		.long sys_clock_settime							// 98

		.long sys_deprecatedsyscall         // 99   // FIXME: was rpi_get_gpio

    .long sys_sigreturn                 // 100
    
    .long sys_thread_event_signal       // 101
    .long sys_thread_event_wait         // 102
    .long sys_thread_event_check        // 103

    .long sys_ismount                   // 104
    
    .long sys_getmsginfo                // 105 TODO: Deprecate, move into fsreq, remove fsreq from iov[0]
    
    .long sys_setsid                    // 106
    .long sys_getsid                    // 107
    
    .long sys_reminterruptserver        // 108
    
    .long sys_thread_event_kevent_mask  // 109
    
    .long sys_thread_create             // 110
    .long sys_thread_join               // 111
    .long sys_thread_exit               // 112
    
    .long sys_thread_gettid             // 113
    .long sys_thread_self               // 114

    .long sys_setpgid                   // 115
    .long sys_getpgid                   // 116

    /*
    .long sys_thread_cancel
    .long sys_thread_setprio
    .long sys_thread_setschedparam    // Not needed?
    
    .long sys_thread_mutex_lock
    .long sys_thread_mutex_unlock

    .long sys_thread_cond_wait
    .long sys_thread_cond_timedwait
    .long sys_thread_cond_signal
    .long sys_thread_cond_broadcast

    .long sys_thread_rwlock_rdlock
    .long sys_thread_rwlock_unlock
    .long sys_thread_rwlock_wrlock
    */

#define UNKNOWN_SYSCALL             0
#define MAX_SYSCALL                 116     // FIXME: Check newlib


/* @brief   System call entry point
 *
 * System calls can take up to 6 parameters in registers R0 to R5.
 * This assembly code saves the register state on the stack before acquiring
 * the Big Kernel Lock (BKL). After the lock is acquired
 * the corresponding sys_xxxx function is called.
 *
 * Any task operating within the kernel is scheduled co-operatively (coroutining).
 * A task switch only occurs if a task blocks, for example due to termination,
 * sleeping or waiting on a condition variable with TaskSleep() in proc/sched.c
 *
 * This is similar to early versions of UNIX-like systems. The Big Kernel Lock
 * acts as the surrounding mutex in a monitor with TaskSleep() and TaskWakeup()
 * operating on a monitor's condition variables.
 *
 * The plan will be to make the kernel pre-emptive at a later date.
 *
 * Upon system call completion this checks if there is any pending signals
 * and pops the registers from the stack.  R0 and R1 are used to return
 * the result of the system call to user space.
 */
.balign 16
swi_vector:
    MSWIPushRegs    
    bl KernelLock
    MEnableInterrupts
    
    ldr lr, [sp, #CONTEXT_PC]
    ldr r7, [lr, #-4]                   // Get syscall nr and check range  (possible to fault, hence getting lock beforehand
    bic r7,r7, #0xff000000              // Eventually handle thumb mode.

    cmp r7, #MAX_SYSCALL
    movgt r7, #UNKNOWN_SYSCALL

    ldr r0, [sp, #CONTEXT_R0]
    ldr r1, [sp, #CONTEXT_R1]
    ldr r2, [sp, #CONTEXT_R2]
    ldr r3, [sp, #CONTEXT_R3]
    
    ldr r5, [sp, #CONTEXT_R5] // Read R4 and R5 from the context
    ldr r4, [sp, #CONTEXT_R4]
    sub sp, #8                // Why not use "push" instruction to push registers on stack ? 
    str r4, [sp, #0]          // Store R4 and R5 on stack, these are the 5th and 6th arguments of a syscall.
    str r5, [sp, #4]          
    ldr r4, =syscall_table
    add r4, r7, LSL #2
    ldr r7, [r4]
    blx r7
    add sp, #8                // Remove R4 and R5 5th and 6th arguments from stack
    str r0, [sp, #CONTEXT_R0]    
    mov r0, sp
    bl check_signals
       
    MDisableInterrupts        
    bl KernelUnlock    
    MSWIPopRegs
    subs pc, lr, #0


/* @brief   Entry point for interrupts
 */
.balign 16
irq_vector:
    sub lr, #4
    MExceptionPushRegs

    mov r0, sp
    bl interrupt_handler

    MExceptionPopRegs
    subs pc, lr, #0


/* @brief   Entry point for undefined instruction exceptions
 */
.balign 16
undef_instr_vector:
    MExceptionPushRegs
    
    mov r0, sp    
    bl undef_instr_handler
    
    MExceptionPopRegs
    subs pc, lr, #0
    

/* @brief   Entry point into kernel for instruction prefetch exceptions
 */
.balign 16
prefetch_abort_vector:
    sub lr, #4
    MExceptionPushRegs

    mov r0, sp    
    bl prefetch_abort_handler

    MExceptionPopRegs
    subs pc, lr, #0

    
/* @brief   Entry point for exceptions caused by failed reads or writes to memory
 */
.balign 16
data_abort_vector:
    sub lr, #8
    MExceptionPushRegs

    mov r0, sp
    bl data_abort_handler

    MExceptionPopRegs   
    subs pc, lr, #0
        

/* ****************************************************************************
 */
.balign 16
reset_vector:
    bl reset_handler
    bl reset_vector

.balign 16    
reserved_vector:
    bl reserved_handler    
    bl reserved_vector

.balign 16
fiq_vector:
    bl fiq_handler
    bl fiq_vector


/* ****************************************************************************
 */

/* @brief   Starting point of a forked process
 *
 */
.balign 16
start_of_forked_process:
    bl start_forked_thread_log
    bl KernelLock    
    mov r0, sp
    bl start_forked_thread_inside_log    
    mov r0, sp
    bl check_signals

    MDisableInterrupts
    bl KernelUnlock
     
    MSWIPopRegs
    subs pc, lr, #0


/* @brief     Tail of an exec'd process.
 *
 * The kernel lock is already obtained upon entry into the exec syscall, so
 * we do not need to obtain it here.  We've set things up to return to new entry
 * point on new stack.
 */
.balign 16
start_of_execed_process:
    mov r0, sp    
    bl check_signals

    MDisableInterrupts
    bl KernelUnlock
     
    MSWIPopRegs
    subs pc, lr, #0

/* @brief   Starting point of a user thread
 *
 */
.balign 16
start_of_user_thread:
    push {r0-r1}
    bl start_kernel_thread_log
    bl KernelLock
    pop {r0-r1}
    blx r1
    
    // Shouldn't get here, we call GetContext() inside arch_init_exec from BootstrapRootProcess
    // FIXME: kernel panic if we do

    mov r1, #0
    bl sys_exit 


/* @brief   Starting point of a kernel thread
 *
 */
.balign 16
start_of_kernel_thread:
    push {r0-r1}
    bl start_kernel_thread_log
    bl KernelLock
    pop {r0-r1}
    blx r1
    
    mov r1, #0
    bl sys_exit 


    
