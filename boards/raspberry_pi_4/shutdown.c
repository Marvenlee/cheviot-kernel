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

#define KDEBUG

#include <kernel/arch.h>
#include <kernel/board/boot.h>
#include <kernel/board/globals.h>
#include <kernel/board/init.h>
#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <kernel/kqueue.h>
#include <string.h>
#include <kernel/board/peripheral_base.h>


/* @brief   Shutdown of the OS
 *
 * Hardware-Specific shutdown routine that turns off power, halts or reboots the system.
 * This can only be performed by the superuser.  It is up to the sysinit process
 * to cleanly shut down the rest of the OS before calling this function.
 */
int sys_shutdown_os(int how)
{
  struct Process *current;
  
  current = get_current_process();
  
  if (is_superuser(current) == false) {
    return -EPERM;  
  }
  
  DisableInterrupts();
  
  while(1);
}

