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


/* @brief   Enable interrupts on current processor
 *
 * Note: Enables only normal interrupts, not fiq
 */
.macro MEnableInterrupts
    cpsie i
.endm


/* @brief   Disable interrupts on current processor and return previous state
 *
 */
.macro MDisableInterrupts
    mrs r0, cpsr        
    cpsid fi                  
    and r0, r0, #0x80   // FIXME: interrupt mask was 0xC0 here int bit and cpu mode irq bit
.endm


/* @brief   Restore interrupt state on current processor
 *
 */
.macro MRestoreInterrupts
    and r0, r0, #0x80     
    mrs r1, cpsr
    orr r1, r1, r0
    msr cpsr_c, r1  
.endm


/* @brief   Get a pointer to the current process
 *
 */
.macro MGetCurrentProcess reg
    ldr \reg, =cpu_table
    ldr \reg, [\reg, #CPU_CURRENT_PROCESS]
.endm


/* @brief   Store register context in a UserContext structure from a SWI interrupt
 *
 */
.macro MSWIPushRegs
    sub sp, #SIZEOF_CONTEXT
    str lr, [sp,#CONTEXT_PC]
    str r0, [sp,#CONTEXT_R0]
  	str	r1, [sp,#CONTEXT_R1]
  	str	r2, [sp,#CONTEXT_R2]	
  	str	r3, [sp,#CONTEXT_R3]
  	str	r4, [sp,#CONTEXT_R4]
  	str	r5, [sp,#CONTEXT_R5]
  	str	r6, [sp,#CONTEXT_R6]
  	str	r7, [sp,#CONTEXT_R7]
  	str	r8, [sp,#CONTEXT_R8]
  	str	r9, [sp,#CONTEXT_R9]
  	str	r10, [sp,#CONTEXT_R10]
  	str	r11, [sp,#CONTEXT_R11]
  	str	r12, [sp,#CONTEXT_R12]
    mrs r0, spsr
  	str	r0, [sp,#CONTEXT_CPSR]

    mrs r3, cpsr

    msr cpsr, #(SYS_MODE | I_BIT | F_BIT)
    mov r0, sp
    mov r1, lr
    msr cpsr, r3
    str r0, [sp,#CONTEXT_SP]
    str r1, [sp,#CONTEXT_LR]
.endm


/* @brief   Reload user registers from a UserContext structure when returning from a SWI interrupt
 *
 */
.macro MSWIPopRegs
    ldr r0, [sp,#CONTEXT_SP]
    ldr r1, [sp,#CONTEXT_LR]

    mrs r3, cpsr
    ldr r2, [sp, #CONTEXT_CPSR]
    mov r2, #SYS_MODE
    orr r2, #(I_BIT | F_BIT)
    msr cpsr_c, r2

    mov sp, r0
    mov lr, r1

    msr cpsr,r3                 
	
	  ldr r0, [sp,#CONTEXT_CPSR]
    msr spsr, r0    
    ldr lr, [sp,#CONTEXT_PC]
    ldr r0, [sp,#CONTEXT_R0]
	  ldr r1, [sp,#CONTEXT_R1]
  	ldr r2, [sp,#CONTEXT_R2]	
  	ldr r3, [sp,#CONTEXT_R3]
	  ldr r4, [sp,#CONTEXT_R4]
  	ldr r5, [sp,#CONTEXT_R5]
  	ldr r6, [sp,#CONTEXT_R6]
	  ldr r7, [sp,#CONTEXT_R7]
	  ldr r8, [sp,#CONTEXT_R8]
	  ldr r9, [sp,#CONTEXT_R9]
	  ldr r10, [sp,#CONTEXT_R10]
	  ldr r11, [sp,#CONTEXT_R11]
	  ldr r12, [sp,#CONTEXT_R12]
    add sp, #SIZEOF_CONTEXT
    
.endm


/* @brief   Store registers in a UserContext structure from an exception or interrupt  
 *
 * Upon entering an exception or interrupt the stack pointer is loaded from
 * the SP register of the mode we are entering.  For exceptions and interrupts, these
 * stack pointers are set in _start in arm.S during startup and are contstant.
 *
 * As interrupt and exceptions may reschedule tasks we need to switch to the
 * per thread SVC kernel stack and store the new register state onto the bottom of
 * this stack.
 *
 * A syscall may have been interrupted or caused an exception so we must place the
 * new stored registers below the current stack pointer.
 *
 * Notes: This is different to the SetContext function in arm.S that is used to save
 *        registers of the current task during a task reschedule.
 */
.macro MExceptionPushRegs
    sub sp, #SIZEOF_ALT
    str lr, [sp, #ALT_PC]
    str r0, [sp, #ALT_R0]
    str r1, [sp, #ALT_R1]
    str r2, [sp, #ALT_R2]
    mrs r1, spsr
    str r1, [sp, #ALT_CPSR]
    
    mov r0, sp
    add sp, #SIZEOF_ALT

    mov r2, #SVC_MODE    
    and r1, #MODE_MASK
    cmp r1, #USR_MODE
    moveq r2, #SYS_MODE

    orr r2, #(I_BIT | F_BIT)
    msr cpsr_c, r2
    mov r1, sp
    mov r2, lr    
    msr cpsr_c, #(SVC_MODE | I_BIT | F_BIT)
    
    sub sp, #SIZEOF_CONTEXT
  	str	r1, [sp,#CONTEXT_SP]
  	str	r2, [sp,#CONTEXT_LR]
  	str	r3, [sp,#CONTEXT_R3]
  	str	r4, [sp,#CONTEXT_R4]
  	str	r5, [sp,#CONTEXT_R5]
  	str	r6, [sp,#CONTEXT_R6]
  	str	r7, [sp,#CONTEXT_R7]
  	str	r8, [sp,#CONTEXT_R8]
  	str	r9, [sp,#CONTEXT_R9]
  	str	r10, [sp,#CONTEXT_R10]
  	str	r11, [sp,#CONTEXT_R11]
  	str	r12, [sp,#CONTEXT_R12]

    ldr r1, [r0, #ALT_CPSR]        // Copy regs temporarily stored on exception/int stack
    ldr r2, [r0, #ALT_R0]          // onto SVC stack.
    ldr r3, [r0, #ALT_R1]
    ldr r4, [r0, #ALT_R2]           
    ldr r5, [r0, #ALT_PC]
    str r1, [sp,#CONTEXT_CPSR]
    str r2, [sp,#CONTEXT_R0]
    str r3, [sp,#CONTEXT_R1]
    str r4, [sp,#CONTEXT_R2]
    str r5, [sp,#CONTEXT_PC]
.endm


/* @brief   Reload registers from a UserContext structure when returning from an exception  
 *
 * This is called from the ARM's SVC Mode which we switched to in MExceptionPushRegs.
 * MexceptionPopRegs pushed the exception's registers on the the SVC stack.
 * 
 * We leave this macro in ABT (Exception) mode
 *
 * Notes: Exceptions can occur in user or kernel mode
 *        This is different to the GetContext function in arm.S that is used to restore
 *        registers during a task reschedule.
 */
.macro MExceptionPopRegs
    mov r0, sp                     // Load several register contents previously stored on
    ldr r1, [sp,#CONTEXT_CPSR]     // the SVC stack.
    ldr r2, [sp,#CONTEXT_R0]                      
    ldr r3, [sp,#CONTEXT_R1]
    ldr r4, [sp,#CONTEXT_R2]
    ldr r5, [sp,#CONTEXT_PC]
    ldr r6, [sp,#CONTEXT_SP]
    ldr r7, [sp,#CONTEXT_LR]
    
    add sp, #SIZEOF_CONTEXT
    msr cpsr_c, #(ABT_MODE | I_BIT | F_BIT)    // Switch to ABT mode as we may be returning
                                               // to SVC, SYS or User mode.

    sub sp, #SIZEOF_ALT            // Store the registers loaded from SVC stack onto the
    str r1, [sp,#ALT_CPSR]         // ABT stack.
    str r2, [sp,#ALT_R0]
    str r3, [sp,#ALT_R1]
    str r4, [sp,#ALT_R2]
    str r5, [sp,#ALT_PC]

    mov r2, #SVC_MODE        
    and r1, #MODE_MASK    
    cmp r1, #USR_MODE
    moveq r2, #SYS_MODE            // We switch to SYS mode so we can access the USR mode 
    orr r2, #(I_BIT | F_BIT)       // stack SP register.
    msr cpsr_c, r2                 // Switch mode to SVC Mode or SYS Mode in order to set
    mov sp, r6                     // SP and LR shadowed registers of SVC or SYS/USR mode     
    mov lr, r7                     // with the contents previously stored on the SVC stack

    msr cpsr_c, #(ABT_MODE | I_BIT | F_BIT)   // Switch back to ABT mode 

  	ldr	r3, [r0,#CONTEXT_R3]       // Load remaining registers from SVC stack
  	ldr	r4, [r0,#CONTEXT_R4]
	  ldr	r5, [r0,#CONTEXT_R5]
  	ldr	r6, [r0,#CONTEXT_R6]
  	ldr	r7, [r0,#CONTEXT_R7]
  	ldr	r8, [r0,#CONTEXT_R8]
  	ldr	r9, [r0,#CONTEXT_R9]
  	ldr	r10, [r0,#CONTEXT_R10]
  	ldr	r11, [r0,#CONTEXT_R11]
  	ldr	r12, [r0,#CONTEXT_R12]

    ldr r0, [sp, #ALT_CPSR]        // Load SPSR with the CPSR to use when we "iret".
    msr spsr, r0
    ldr r0, [sp, #ALT_R0]
    ldr r1, [sp, #ALT_R1]
    ldr r2, [sp, #ALT_R2]           
    ldr lr, [sp, #ALT_PC]

    add sp, #SIZEOF_ALT            // Adjust ABT stack upwards to original position.
                                   // We leave this macro in ABT mode.
.endm

