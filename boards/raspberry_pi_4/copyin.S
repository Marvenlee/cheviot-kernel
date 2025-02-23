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
.global CopyIn
.global CopyInString
.global CopyOut
.global CopyUserString
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
CopyIn:
    push {r0-r1}
    Mget_current_thread r0
    ldr r1, =CopyInCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
CopyInLoop:
    cmp r2, #0
    beq CopyInDone
    ldrbT r3, [r1], #1
    strb r3, [r0], #1
    sub r2, #1
    b CopyInLoop
CopyInDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
CopyInCatch:
    mov r0, #-1
    bx lr


/* @brief   Copy a string from user-space into a kernel buffer
 *
 * @param   dst, address of buffer in kernel space to copy string to 
 * @param   src, address of string in user space to copy from
 * @param   sz, size of destination buffer
 * @return  0 on success, -1 on error
 */
CopyInString:
    push {r0-r1}
    Mget_current_thread r0
    ldr r1, =CopyInStringCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
CopyInStringLoop:
    cmp r2, #0
    beq CopyInStringNoTermDone
    ldrbT r3, [r1], #1
    strb r3, [r0], #1
    cmp r3, #0
    beq CopyInStringDone
    sub r2, #1
    b CopyInStringLoop
CopyInStringDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
CopyInStringNoTermDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
CopyInStringCatch:
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
CopyOut:
    push {r0-r1}
    Mget_current_thread r0
    ldr r1, =CopyOutCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
CopyOutLoop:
    cmp r2, #0
    beq CopyOutDone
    ldrb r3, [r1], #1
    strbT r3, [r0], #1
    sub r2, #1    
    b CopyOutLoop
CopyOutDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
CopyOutCatch:
    mov r0, #-1
    bx lr


/* @brief   Copy a string from kernel to user-space
 *
 */
CopyUserString:
    push {r0-r1}
    Mget_current_thread r0
    ldr r1, =CopyUserStringCatch
    str r1, [r0, #TASK_CATCH_PC]
    pop {r0-r1}
CopyUserStringLoop:
    cmp r2, #0
    beq CopyUserStringNoTermDone
    ldrbT r3, [r1], #1
    strbT r3, [r0], #1
    cmp r3, #0
    beq CopyUserStringDone
    sub r2, #1
    b CopyUserStringLoop
CopyUserStringDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #0
    bx lr
CopyUserStringNoTermDone:
    Mget_current_thread r0
    ldr r1, =#0xfee15bad
    str r1, [r0, #TASK_CATCH_PC]
    mov r0, #-1
    bx lr
CopyUserStringCatch:
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

