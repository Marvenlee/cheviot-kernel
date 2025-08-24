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
#include <signal.h>
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
 
  futex_free(current_proc, futex);
  
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

  if (((uintptr_t)uaddr % sizeof(int)) != 0) {
    return NULL;
  }
  
  hash = futex_hash(proc, uaddr);
  
  futex = LIST_HEAD(&futex_hash_table[hash]);

  while (futex != NULL) {
    if (futex->uaddr == (uintptr_t)uaddr && futex->proc == proc) {
      return futex;
    }
  
    futex = LIST_NEXT(futex, hash_link);
  }    

  if (flags & FUTEX_CREATE) {
    futex = futex_create(proc, uaddr);
    
    if (futex == NULL) {
      sys_exit(SIGKILL<<8);
    }    
  }

  return futex;
}


/*
 *
 */
uint32_t futex_hash(struct Process *proc, void *uaddr)
{
  uint32_t hash;
  
  hash = ((uintptr_t)uaddr + proc->pid) % FUTEX_HASH_SZ;
  return hash;
}  


/*
 *
 */
int lock_futex_table(void)
{
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
  futex_table_busy = false;
  TaskWakeup(&futex_table_busy_rendez);
}


/*
 * We are currently creating futexes on first use, such as the first time a particular mutex is locked.
 * We could potentially run out of mutexes and not all code checks the error code of pthread_mutex_lock.
 * This could potentially cause issues.  We could have an "out-of-futex" killer than exits the process.
 *
 * Prefer to have syscall to create futex at init time.  If a mutex is not valid or uninitialized on use
 * then kill the process by default with SIGSEGV.
 *
 * Futexes then need to be duplicated to new process on fork().
 *
 * TODO: exec() must release futexes.
 */
struct Futex *futex_create(struct Process *proc, void *uaddr)
{
  struct Futex *futex;

  futex = LIST_HEAD(&free_futex_list);

  if (futex == NULL) {
    Error("no free futex");
    return NULL;
  }

  LIST_REM_HEAD(&free_futex_list, link);
  LIST_ADD_HEAD(&proc->futex_list, futex, link);

  futex->hash = futex_hash(proc, uaddr);  
  LIST_ADD_HEAD(&futex_hash_table[futex->hash], futex, hash_link);

  futex->proc = proc;  
  futex->uaddr = (uintptr_t)uaddr;
    
  InitRendez(&futex->rendez);
  
  return futex;
}


/*
 *
 */
void futex_free(struct Process *proc, struct Futex *futex)
{
  LIST_REM_ENTRY(&futex_hash_table[futex->hash], futex, hash_link);

  LIST_REM_ENTRY(&proc->futex_list, futex, link);
  LIST_ADD_HEAD(&free_futex_list, futex, link);
}


/*
 *
 */
int fini_futexes(struct Process *proc)
{
  int sc;

  lock_futex_table();
  sc = do_cleanup_futexes(proc);
  unlock_futex_table();

  return sc;
}


/*
 * Where else is this called, does it have futex table lock?
 */
int do_cleanup_futexes(struct Process *proc)
{
  struct Futex *futex, *next;
  
  futex = LIST_HEAD(&proc->futex_list);

  while (futex != NULL) {
    next = LIST_NEXT(futex, link);
    
    if (futex->proc == proc) {
      LIST_REM_ENTRY(&futex_hash_table[futex->hash], futex, hash_link);

      LIST_REM_ENTRY(&proc->futex_list, futex, link);
      LIST_ADD_HEAD(&free_futex_list, futex, link);
    }

    futex = next;
  }
  
  return 0;
}


