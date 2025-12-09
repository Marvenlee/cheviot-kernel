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
.global copyin
.global copyinstring
.global copyout
.global copyoutstring
.global StrLenUser


.section .text

/* @brief   Copy data from a user-space buffer into a kernel buffer
 *
 * @param   dst, destination pointer in kernel space
 * @param   src, source pointer in user space
 * @param   sz, size of buffer to copy
 * @return  0 on success, -1 on error
 *
 * TODO: Currently doing byte-at-a-time copy, do word-at-a-time and multiple registers
 * for larger transfers
 */ 
copyin:
    push {r0-r1}
    Mget_current_thread r0
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
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyinCatch:
    mov r0, #-1
    bx lr


/* @brief   Copy a string from user-space into a kernel buffer
 *
 * @param   dst, address of buffer in kernel space to copy string to 
 * @param   src, address of string in user space to copy from
 * @param   sz, size of destination buffer
 * @return  0 on success, -1 on error
 */
copyinstring:
    push {r0-r1}
    Mget_current_thread r0
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
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyinstringNoTermDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
copyinstringCatch:
    mov r0, #-1
    bx lr


/* @brief   Copy data from a kernel buffer into a user-space buffer
 *
 * @param   dst, destination pointer in user space
 * @param   src, source pointer in kernel space
 * @param   sz, size of buffer to copy
 * @return  0 on success, -1 on error
 *
 * TODO: Currently doing byte-at-a-time copy, do word-at-a-time and multiple registers
 * for larger transfers
 */ 
copyout:
    push {r0-r1}
    Mget_current_thread r0
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
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyoutCatch:
    mov r0, #-1
    bx lr


/* @brief   Copy a string from kernel to user-space
 *
 */
copyoutstring:
    push {r0-r1}
    Mget_current_thread r0
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
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
copyoutstringNoTermDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
copyoutstringCatch:
    mov r0, #-1
    bx lr


/* @brief   Measure the length of a string in user-space
 *
 * FIXME: Not returning length of string.
 * 
 * @param   str, string to measure in user-space
 * @param   max_len,  maximum length of string to determine length of
 * @return  length of string or negative errno on error or page fault.
 *
 * Note: The string can be modified or corrupted by other threads in user-space.
 */
StrLenUser:
    push {r0-r1}
    Mget_current_thread r0
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
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    // FIXME: StrLenUser Return length of string, not 0
    mov r0, #0
    bx lr
StrLenUserNoTermDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
StrLenUserCatch:
    mov r0, #-1
    bx lr

