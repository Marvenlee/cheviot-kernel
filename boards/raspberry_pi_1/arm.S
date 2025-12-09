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

/*
 * Kernel entry point and general ARM CPU functions
 */ 

#include <kernel/board/arm.i>
#include <kernel/board/task.i>
#include <kernel/board/macros.i>

// Stacks : TODO:  Some will become per-processor
.global svc_stack_top
.global svc_stack
.global interrupt_stack_top
.global interrupt_stack
.global exception_stack_top
.global exception_stack
.global idle_stack_top
.global idle_stack

// Functions
.global _start
.global get_current_process
.global get_cpu
.global DisableInterrupts
.global RestoreInterrupts
.global EnableInterrupts
.global SpinLock
.global SpinUnlock
.global SpinLockIRQSave
.global SpinUnlockIRQRestore
.global copyin
.global copyinstring
.global copyout
.global copyoutstring
.global StrLenUser
.global SetContext
.global GetContext


.section .bss

/* @brief   Memory reserved for kernel stacks
 */
.balign 4096
svc_stack:
.skip 8092
svc_stack_top:

interrupt_stack:
.skip 8092
interrupt_stack_top:

exception_stack:
.skip 8092
exception_stack_top:

idle_stack:
.skip 4096
idle_stack_top:

init_stack:
.skip 4096
init_stack_top:


.section .text

/* @brief   Entry point into the kernel
 *
 * A pointer to a bootinfo structure is passed in r0 by the bootloader.
 * Paging is already enabled by the bootloader and we begin executing at
 * the kernel's _start virtual address.
 *
 * This sets up an initial stack pointer in the kernel,  clears uninitialized data
 * and calls the Main() initalization function in arm/main.c with bootinfo as a
 * parameter.
 */
_start:
    ldr   sp, =init_stack_top
    ldr   r3, =_ebss
    ldr   r2, =_sbss
    cmp   r2, r3
    bcs   2f
    sub   r3, r2, #1
    ldr   r1, =_ebss
    sub   r1, r1, #1
    mov   r2, #0
1:
    strb   r2, [r3, #1]!
    cmp   r3, r1
    bne   1b

    ldr r4, =bootinfo
    str r0, [r4]
2:


# FIXME: Add or remove this floating point initialization.
#   mrc p15, 0, r0, c1, c0, 2
#   orr r0, r0, #0xC00000            /* double precision */
#   mcr p15, 0, r0, c1, c0, 2
#   mov r0, #0x40000000
#   fmxr fpexc,r0

    msr cpsr_c, #(FIQ_MODE | I_BIT | F_BIT);
    ldr sp, =interrupt_stack_top

    msr cpsr_c, #(IRQ_MODE | I_BIT | F_BIT);
    ldr sp, =interrupt_stack_top

    msr cpsr_c, #(ABT_MODE | I_BIT | F_BIT);
    ldr sp, =exception_stack_top

    msr cpsr_c, #(UND_MODE | I_BIT | F_BIT);
    ldr sp, =exception_stack_top

    msr cpsr_c, #(SYS_MODE | I_BIT | F_BIT);
    ldr sp, =idle_stack_top

    msr cpsr_c, #(SVC_MODE | I_BIT | F_BIT);
    ldr sp, =svc_stack_top

    b Main


/* @brief   Get the Process structure of the caller.
 */
get_current_process:
    MGetCurrentProcess r0
    bx lr


/* @brief   Get the CPU structure of the caller.
 */
get_cpu:
    ldr r0, =cpu_table
    bx lr


/* @brief Disable interrupts on current CPU and return previous interrupt state
 * 
 * @return  int_state, interrupt flags prior to disabling interrupts
 */
DisableInterrupts:
    MDisableInterrupts
    bx lr


/* @brief   Restore interrupts on current CPU to a previous state
 *
 * @param   int_state, state of interrupt flags to restore
 */
RestoreInterrupts:
    MRestoreInterrupts
    bx lr


/* @brief   Enable interrupts on current CPU
 */
EnableInterrupts:
    MEnableInterrupts
    bx lr


/* @brief   Acquire a spinlock
 *
 * For current single CPU we ignore the spinlock
 */
SpinLock:
    bx lr


/* @brief   Release a spinlock
 *
 * For current single CPU we ignore the spinlock
 */
SpinUnlock:
    bx lr


/* @brief   Disable interrupts and acquire spinlock
 *
 * For current single CPU we ignore the spinlock
 */
SpinLockIRQSave:
    MDisableInterrupts    
    bx lr


/* @brief   release spinlock and restore interrupts
 *
 * SpinUnlockIRQRestore(int_state_t state, spinlock_t lock)
 *
 * For current single CPU we ignore the spinlock
 */
SpinUnlockIRQRestore:
    MRestoreInterrupts
    bx lr



// @brief   Copy data from a user-space buffer into a kernel buffer
//
// @param   dst, destination pointer in kernel space
// @param   src, source pointer in user space
// @param   sz, size of buffer to copy
// @return  0 on success, -1 on error
// 
copyin:
    push {r0-r1}
    MGetCurrentProcess r0
    ldr r1, =copyinCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
copyinLoop:
    cmp r2, #0
    beq copyinDone
    ldrbT r3, [r1], #1
    strb r3, [r0], #1
    sub r2, #1
    b copyinLoop
copyinDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyinCatch:
    mov r0, #-1
    bx lr


// @brief   Copy a string from user-space into a kernel buffer
//
// @param   dst, address of buffer in kernel space to copy string to 
// @param   src, address of string in user space to copy from
// @param   sz, size of destination buffer
// @return  0 on success, -1 on error
//
copyinstring:
    push {r0-r1}
    MGetCurrentProcess r0
    ldr r1, =copyinstringCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
copyinstringLoop:
    cmp r2, #0
    beq copyinstringNoTermDone
    ldrbT r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq copyinstringDone
    sub r2, #1
    b copyinstringLoop
copyinstringDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyinstringNoTermDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
copyinstringCatch:
    mov r0, #-1
    bx lr


// @brief   Copy data from a kernel buffer into a user-space buffer
//
// @param   dst, destination pointer in user space
// @param   src, source pointer in kernel space
// @param   sz, size of buffer to copy
// @return  0 on success, -1 on error
// 
copyout:
    push {r0-r1}
    MGetCurrentProcess r0
    ldr r1, =copyoutCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
copyoutLoop:
    cmp r2, #0
    beq copyoutDone
    ldrb r3, [r1], #1
    strbT r3, [r0], #1
    sub r2, #1    
    b copyoutLoop
copyoutDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyoutCatch:
    mov r0, #-1
    bx lr


# ****************************************************************************
# copyoutstring

copyoutstring:
    push {r0-r1}
    MGetCurrentProcess r0
    ldr r1, =copyoutstringCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
copyoutstringLoop:
    cmp r2, #0
    beq copyoutstringNoTermDone
    ldrbT r3, [r1], #1
    strbT r3, [r0], #1
    cmp r3, #0
    beq copyoutstringDone
    sub r2, #1
    b copyoutstringLoop
copyoutstringDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyoutstringNoTermDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
copyoutstringCatch:
    mov r0, #-1
    bx lr


# ****************************************************************************
# len = StrLenUser (str, MAX_LEN);
# r0 = str, r1 = MAX_LEN;

StrLenUser:
    push {r0-r1}
    MGetCurrentProcess r0
    ldr r1, =StrLenUserCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
StrLenUserLoop:
    cmp r2, #0
    beq StrLenUserNoTermDone
    ldrbT r3, [r1], #1
    //    strbT r3, [r0], #1
    cmp r3, #0
    beq StrLenUserDone
    sub r2, #1
    b StrLenUserLoop
StrLenUserDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    // FIXME: StrLenUser Return length of string, not 0
    mov r0, #0
    bx lr
StrLenUserNoTermDone:
    MGetCurrentProcess r0
    ldr r1, =#0xdeadbeef
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
StrLenUserCatch:
    mov r0, #-1
    bx lr


// @brief   Save register context to memory
//
// @param   context, pointer to 60 byte buffer containing the register context
//
// Saves the current register context to a buffer. This is used by the Reschedule() 
// function in proc/sched.c to switch tasks.
//
SetContext:
	str	r1, [r0,#4]
	str	r2, [r0,#8]
	str	r3, [r0,#12]
	str	r4, [r0,#16]
	str	r5, [r0,#20]
	str	r6, [r0,#24]
	str	r7, [r0,#28]
	str	r8, [r0,#32]
	str	r9, [r0,#36]
	str	r10, [r0,#40]
	str	r11, [r0,#44]
	str	r12, [r0,#48]
	str	r13, [r0,#52]
	str	r14, [r0,#56]
	/* store 1 as r0-to-restore */
	mov	r1, #1
	str	r1, [r0]
	/* return 0 */
	mov	r0, #0
	mov	pc, lr


// @brief   Load register context from memory
//
// @param   context, pointer to 60 byte buffer containing the register context
//
// Switches to a previously stored context. This is used by the Reschedule() 
// function in proc/sched.c to switch tasks.
//
GetContext:
	ldr	r1, [r0,#4]
	ldr	r2, [r0,#8]
	ldr	r3, [r0,#12]
	ldr	r4, [r0,#16]
	ldr	r5, [r0,#20]
	ldr	r6, [r0,#24]
	ldr	r7, [r0,#28]
	ldr	r8, [r0,#32]
	ldr	r9, [r0,#36]
	ldr	r10, [r0,#40]
	ldr	r11, [r0,#44]
	ldr	r12, [r0,#48]
	ldr	r13, [r0,#52]
	ldr	r14, [r0,#56]
	ldr	r0, [r0]
	mov	pc, lr

.end

