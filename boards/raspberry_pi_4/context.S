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
#include <machine/board/hal_arm.i>


// Exports
.global get_current_process
.global get_current_thread
.global get_cpu
.global SetContext
.global GetContext
.global save_fp_context


.section .text

/* @brief   Get the Process structure of the caller.
 */
get_current_process:
    MGetCurrentProcess r0
    bx lr


/* @brief   Get the Process structure of the caller.
 */
get_current_thread:
    Mget_current_thread r0
    bx lr


/* @brief   Get the CPU structure of the caller.
 */
get_cpu:
    ldr r0, =cpu_table
    bx lr


/* @brief   Save register context to memory
 *
 * @param   context, pointer to 60 byte buffer containing the register context
 * @return  0 on calling GetContext, 1 when GetContext is called and returns from this.
 *
 * Saves the current register context to a buffer. This is used by the Reschedule() 
 * function in proc/sched.c to switch tasks.
 *
 * This is different from the MSWIPushRegs and MSWIExceptionPushRegs in macros.i
 * that push registers on entry to a sytem call, exception or interrupt.
 *
 * SetContext and GetContext is called in the following way:
 *
 * if (SetContext(&context[0]) == 0) {
 *   GetContext(next->context);
 * }
 *
 * The current thread calls SetContext to save the register state. It returns 0 to indicate
 * it should call GetContext to get the next task's register state. When GetContext reloads
 * the next task's register state it resumes at the point of where it would be returning
 * from it's call to SetContext. To avoid an infinite loop, we make SetContext return 1 to
 * indicate we now have the new task's context and avoid calling GetContext.
 *
 * TODO: Save FPU Floating Point register state
 */
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

  /* Save FP status register and FP registers */
	vmrs r1,fpscr
	str r1, [r0, #60]
	
	mov r1, r0
	add r1, #64
	
	vstm r1, {s0-s15}
	add r1, #64
	vstm r1, {s16-s31}	
	
	/* store 1 as r0-to-restore */
	mov	r1, #1
	str	r1, [r0]
	/* return 0 */
	mov	r0, #0
	mov	pc, lr


/* @brief   Load register context from memory
 *
 * @param   context, pointer to 60 byte buffer containing the register context
 *
 * Switches to a previously stored context stored by SetContext. This is used by
 * the Reschedule() function in proc/sched.c to switch tasks.  The code resumes
 * with the new task's register context at the point it returns from SetContext,
 * except SetContext will this time return 1.
 *
 * This is different from the MSWIPopRegs and MSWIExceptionPopRegs in macros.i
 * that pop registers on exit from a sytem call, exception or interrupt.
 *
 * TODO: Restore FPU Floating Point register state
 */
GetContext:
  /* Load FP registers and FP status register */

	mov r1, r0
	add r1, #64
	
	vldm r1, {s0-s15}
	add r1, #64
	vldm r1, {s16-s31}

	ldr r1, [r0, #60]	
	vmsr fpscr, r1	

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


/*
 *
 */
save_fp_context:
  /* Save FP status register and FP registers */
	vmrs r1,fpscr
	str r1, [r0, #0]	
	add r0, #4
	
	vstm r0, {s0-s15}
	vstm r0, {s16-s31}	

  mov pc, lr    
  
.end

