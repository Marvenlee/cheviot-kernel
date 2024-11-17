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

//#define KDEBUG

#include <kernel/board/elf.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/signal.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <stdint.h>
#include <sys/privileges.h>


/* @brief   Restrict process permissions to perform certain system calls.
 *
 */
int sys_set_privileges(int when, uint64_t *_set, uint64_t *_result)
{
  struct Process *current_proc;
  uint64_t set, result;
  int sc;
    
  current_proc = get_current_process();
  
  if (_set == NULL) {
    return -EINVAL;
  }
  
  if (CopyIn(&set, _set, sizeof set) != 0) {
    return -EFAULT;
  }

  switch(when) {
    case PRIV_NOW:
      current_proc->privileges &= set;
      result = current_proc->privileges;
      sc = 0;
      break;
    case PRIV_AFTER_EXEC:
      current_proc->privileges_after_exec &= set;
      result = current_proc->privileges_after_exec;
      sc = 0;
      break;
    default: 
      sc = -EINVAL;
      break; 
  }
  
  if (sc == 0 && _result != NULL) {
    CopyOut(_result, &result, sizeof result);
  }
  
  return sc;
}


/* @brief   Check if a process is allowed to perform privileged I/O operations
 *
 * @param   proc, process to check
 * @return  true if it has privileges to perform I/O operations, false otherwise
 *
 * TODO: Replace with a function to check against the current privilege bitmap.
 *       check_privilege(proc, uint64_t privilege);
 */
int check_privileges(struct Process *proc, uint64_t map)
{
  if ((proc->privileges & map) != map) {
    return -EPERM;
  }
  
  return 0;
}


/*
 *
 */
void fork_privileges(struct Process *new_proc, struct Process *parent)
{
  new_proc->privileges = parent->privileges;
  new_proc->privileges_after_exec = parent->privileges;
}


/*
 *
 */
void init_privileges(struct Process *proc)
{
  proc->privileges = PRIV_PERMIT_ALL;
  proc->privileges_after_exec = PRIV_PERMIT_ALL;
}


/*
 *
 */
void exec_privileges(struct Process *proc)
{
  proc->privileges &= proc->privileges_after_exec;
  proc->privileges_after_exec = proc->privileges;
}


