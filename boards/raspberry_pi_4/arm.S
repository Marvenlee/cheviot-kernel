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
#include <machine/board/hal_arm.i>

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
.global idle_cpu
.global DisableInterrupts
.global RestoreInterrupts
.global EnableInterrupts
.global SpinLock
.global SpinUnlock
.global SpinLockIRQSave
.global SpinUnlockIRQRestore


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
    
    // Clear the BSS section as it's size > 1.
    sub   r3, r2, #1
    ldr   r1, =_ebss
    sub   r1, r1, #1
    mov   r2, #0
1:
    strb   r2, [r3, #1]!
    cmp   r3, r1
    bne   1b

2:
    ldr r4, =bootinfo
    str r0, [r4]


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



/* @brief   Put the CPU into an idle mode and wait for an interrupt
 */
idle_cpu:
  wfi
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


