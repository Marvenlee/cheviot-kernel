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
 * Debugging functions and Debug() system call.
 */

#include <kernel/board/peripheral_base.h>
#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/board/aux_uart.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/filesystem.h>
#include <stdarg.h>
#include <string.h>


/* @brief Perform initialization of the kernel logger
 */
void arch_debug_init(void)
{
  aux_uart_init();
}


/*
 */
void arch_debug_print_char(char ch)
{
  aux_uart_write_byte(ch);      
}


/*
 *
 */
void arch_debug_print_user_context(void *user_context)
{
  struct UserContext *uc = (struct UserContext *)user_context;

  DoLog("pc = %08x,   sp = %08x", uc->pc, uc->sp);
  DoLog("lr = %08x, cpsr = %08x", uc->lr, uc->cpsr);
  DoLog("r0 = %08x,   r1 = %08x", uc->r0, uc->r1);
  DoLog("r2 = %08x,   r3 = %08x", uc->r2, uc->r3);
  DoLog("r4 = %08x,   r5 = %08x", uc->r4, uc->r5);
  DoLog("r6 = %08x,   r7 = %08x", uc->r6, uc->r7);
  DoLog("r8 = %08x,   r9 = %08x", uc->r8, uc->r9);
  DoLog("r10 = %08x,  r11 = %08x   r12 = %08x", uc->r10, uc->r11, uc->r12);
}


/*
 */
void arch_kernel_panic(void)
{
  // TODO: IPI to panic other processors.
  
  DisableInterrupts();
  while(1);
}



