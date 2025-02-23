/*
 * Copyright 2024  Marven Gilhespie
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
#include <sys/execargs.h>
#include <sys/wait.h>


/* @brief   Free a futex
 */
int sys_futex_destroy(void *uaddr)
{
  struct Futex *futex;
  struct Process *current_proc;

  current_proc = get_current_process();

  lock_futex_table();

  futex = futex_get(current_proc, uaddr, 0);

  if (futex == NULL) {
    unlock_futex_table();
    return -EINVAL;
  }
  
  LIST_REM_ENTRY(&futex_hash_table[futex->hash], futex, hash_link);  
  LIST_ADD_HEAD(&free_futex_list, futex, free_link);
  
  unlock_futex_table();

  return 0;
}


/*
 *
 */
int sys_futex_wait(void *uaddr, uint32_t val, const struct timespec *timeout, int flags)
{
  struct Futex *futex;
  uint32_t cval;
  struct timespec ts;
  struct Process *current_proc;
  
  current_proc = get_current_process();
  
  lock_futex_table();
  
  if (CopyIn(&cval, uaddr, sizeof cval) != 0) {
    Error("-- failed to copyin uaddr");
    unlock_futex_table();
    return -EFAULT;
  }
  
  if (cval != val) {
    Error("-- cval != val, EAGAIN");
    unlock_futex_table();
		return -EAGAIN;
  }
  
  if (timeout != NULL) {
    if (CopyIn(&ts, timeout, sizeof ts) == 0) {
      Error("-- timeout copyin failed");
      unlock_futex_table();
  		return -EFAULT;
    }
    
    timeout = &ts;
  }
  
  futex = futex_get(current_proc, uaddr, FUTEX_CREATE);

  if (futex == NULL) {
    Error("-- cannot find mutex for uaddr:%08x", uaddr);
    unlock_futex_table();
    return -EINVAL;
  }

  unlock_futex_table();
  TaskSleep(&futex->rendez);
    
  return 0;
}


/*
 *
 */
int sys_futex_wake(void *uaddr, uint32_t n, int flags)
{
	return sys_futex_requeue(uaddr, n, NULL, 0, flags);
}


/*
 * Note: The requeue does the cond_wakeup BUT takes the mutex's queue as uaddr2,
 * the opposite of cond_wait and cond_wakeup.
 */
int sys_futex_requeue(void *uaddr, uint32_t n, void *uaddr2, uint32_t m, int flags)
{
  int_state_t int_state;
  struct Futex *futex, *futex2;
	uint32_t count = 0;
  struct Process *current_proc;
  struct Thread *thread;
 
  Error("sys_futex_requeue");
  
  current_proc = get_current_process();

  if (uaddr == uaddr2) {
    return -EINVAL;
  }

  lock_futex_table();

  futex = futex_get(current_proc, uaddr, FUTEX_CREATE);

	if (futex == NULL) {
    unlock_futex_table();
		return 0;
  }

  if (uaddr2 != NULL) {
		futex2 = futex_get(current_proc, uaddr2, FUTEX_CREATE);

    if (futex2 == NULL) {
      unlock_futex_table();
      return 0;
    }
  }  

  int_state = DisableInterrupts();
  
	while ((thread = LIST_HEAD(&futex->rendez.blocked_list)) != NULL && (count < (n + m))) {    
		if (count < n) {
		  TaskWakeupSpecific(thread, INTRF_NONE);
		} else if (uaddr2 != NULL) {
		  TaskRendezRequeue(thread, &futex->rendez, &futex2->rendez);
		}

    RestoreInterrupts(int_state);
		count++;
    int_state = DisableInterrupts();
	}
	
  RestoreInterrupts(int_state);

  unlock_futex_table();
	
	return count;
}


/*
 *
 */
struct Futex *futex_get(struct Process *proc, void *uaddr, int flags)
{
  struct Futex *futex;
  int hash;

  Info("futex_get(proc:%08x, uaddr:%08x)", (uint32_t)proc, (uint32_t)uaddr);
  
  if (((uintptr_t)uaddr % sizeof(int)) != 0) {
    return -EINVAL;
  }
  
  hash = futex_hash(proc, uaddr);
  
  futex = LIST_HEAD(&futex_hash_table[hash]);

  while (futex != NULL) {
    if (futex->uaddr == uaddr && futex->proc == proc) {
      return futex;
    }
  
    futex = LIST_NEXT(futex, hash_link);
  }    
  
  if (flags & FUTEX_CREATE) {
    futex = futex_create(proc, uaddr);
  }
  
  return futex;
}


/*
 *
 */
uint32_t futex_hash(struct Process *proc, void *uaddr)
{
  uintptr_t iaddr = (uintptr_t)iaddr;
  uint32_t hash;
  
  hash = (iaddr + proc->pid) % FUTEX_HASH_SZ;
  return hash;
}  


/*
 *
 */
int lock_futex_table(void)
{
  Error("lock_futex_table");

  while(futex_table_busy == true) {
    TaskSleep(&futex_table_busy_rendez);
  }

  futex_table_busy = true;
  return 0;
}


/*
 *
 */
void unlock_futex_table(void)
{
  Error("unlock_futex_table");

  futex_table_busy = false;
  TaskWakeup(&futex_table_busy_rendez);
}


/*
 * TODO: Remove, must by created on the fly, so new forked process can use existing initialized mutexes
 * in the new address space.
 *
 * TODO: Exec must release futexes.
 */
struct Futex *futex_create(struct Process *proc, void *uaddr)
{
  struct Futex *futex;

  Error("futex_create(proc:%08x, uaddr:%08x)", (uint32_t)proc, (uint32_t)uaddr);

  futex = LIST_HEAD(&free_futex_list);

  if (futex == NULL) {
    Error("no free futex");
    unlock_futex_table();
    return NULL;
  }

  LIST_REM_HEAD(&free_futex_list, free_link);

  futex->hash = futex_hash(proc, uaddr);  
  LIST_ADD_HEAD(&futex_hash_table[futex->hash], futex, hash_link);

  futex->proc = proc;  
  futex->uaddr = uaddr;
    
  InitRendez(&futex->rendez);
  
  Info("futex:%08x", (uint32_t)futex);
  
  return futex;
}

  
/*
 *
 */
int cleanup_futexes(struct Process *proc)
{
  struct Futex *futex, *next;
  
  for (int h = 0; h < FUTEX_HASH_SZ; h++) {
    futex = LIST_HEAD(&futex_hash_table[h]);
    
    while (futex != NULL) {
      next = LIST_NEXT(futex, hash_link);
      
      if (futex->proc == proc) {
        LIST_REM_ENTRY(&futex_hash_table[h], futex, hash_link);  
        LIST_ADD_HEAD(&free_futex_list, futex, free_link);              
      }
      
      futex = next;
    }
  }
  
  return 0;
}


