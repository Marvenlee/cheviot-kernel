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
#include <sys/execargs.h>
#include <sys/wait.h>


/* @brief   Fork the calling process
 *
 * @return  In parent process, a positive non-zero Process ID of the child process
 *          In child process, 0 is returned
 *          On error a negative errno value is returned.
 */
int sys_fork(void)
{
  struct Process *current_proc;
  struct Process *new_proc;
  struct Thread *current_thread;
  struct Thread *new_thread;
//  pid_t pid;
  
	Info("sys_fork()");

  current_proc = get_current_process();
  current_thread = get_current_thread();

  if ((new_proc = alloc_process(current_proc, current_proc->flags, current_proc->basename)) == NULL) {
    Info("fork alloc_process failed");
    return -ENOMEM;
  }

  if (fork_address_space(&new_proc->as, &current_proc->as) != 0) {
    Info("fork_address_space failed");
    free_process(new_proc);
    return -ENOMEM;
  }

  new_thread = fork_thread(new_proc, current_proc, current_thread);

  if (new_thread == NULL) {
    Info("fork_thread failed");
    free_address_space(&new_proc->as);
    free_process(new_proc);
    return -ENOMEM;
  }

  fork_ids(new_proc, current_proc);
  fork_process_fds(new_proc, current_proc);
  fork_signals(new_proc, current_proc);  
  
	Info("new proc:%08x, current_proc:%08x", (uint32_t)new_proc, (uint32_t)current_proc);

  activate_pid(new_proc->pid);

  Info("fork starting new_thread %d", new_thread->tid);
  thread_start(new_thread);

  Info("fork parent returning pid:%d", new_proc->pid);

  return new_proc->pid;
}


/* @brief   Exit the current process.
 * 
 * @param   Exit status to return to parent
 */
void sys_exit(int status)
{
  struct Process *current;
  struct Process *parent;
  struct Thread *current_thread;
  
  Info("sys_exit(%d)", status);
  
  current_thread = get_current_thread();
  current = get_current_process();
  parent = current->parent;

  Info("got current_thread, current and parent");

  KASSERT (parent != NULL);

  if (current->exit_in_progress == false) {
    Info("exit in progress is false, first exit");
    
    current->exit_status = status;
    current->exit_in_progress = true;

    do_kill_other_threads_and_wait(current, current_thread);

    fini_fproc(current);    
    cleanup_address_space(&current->as);

    detach_child_processes(current);
    

    // TODO: Cancel any alarms
    
    // TODO: send SIGFCHLD to parent process
    // parent->usignal.sig_pending |= SIGFCHLD;	
    //	parent->usignal.siginfo_data[SIGCHLD-1].si_signo = SIGCHLD;
    //	parent->usignal.siginfo_data[SIGCHLD-1].si_code = 0;
    //	parent->usignal.siginfo_data[SIGCHLD-1].si_value.sival_int = 0;
        
  }
  
  // The last thread to exit changes proc->state to PROC_STATE_EXITED and signals parent process
  do_exit_thread(0);
}


/* @brief   Wait for child processes to exit
 *
 * @param   pid,
 * @param   status,
 * @param   options
 * @return  PID of exited child process or negative errno on failure  
 */
int sys_waitpid(int pid, int *status, int options)
{
  struct Process *current;
  struct Process *child;
  bool found = false;
  int found_in_pgrp = 0;
  int err = 0;
  
  Info("sys_waitpid(pid:%d, opt:%08x", pid, options);
  
  current = get_current_process();

  if (-pid >= max_process || pid >= max_process) {
    Error("waitpid %d invalid pid", pid);
    return -EINVAL;
  }

  while (!found) // && pending_signals
  {
    if (pid > 0) {
      found = false;

      child = get_process(pid);
      if (child != NULL && child->parent == current) {
        if (child->state == PROC_STATE_EXITED) {
          found = true;
        }
      } else {
        err = -EINVAL;
        goto exit;
      }
    } else if (pid == 0) {
      // Look for any child process that is a zombie and belongs to current
      // process group
      // check if any belong to current process group, exit if none.e

      child = LIST_HEAD(&current->child_list);

      if (child == NULL) {
        err = -EINVAL;
        goto exit;
      }

      found = false;
      found_in_pgrp = 0;

      while (child != NULL) {
        if (child->pgrp == current->pgrp) {
          found_in_pgrp++;

          if (child->state == PROC_STATE_EXITED) {
            found = true;
            break;
          }
        }

        child = LIST_NEXT(child, child_link);
      }

      if (found_in_pgrp == 0) {
        err = -EINVAL;
        goto exit;
      }
    } else if (pid == -1) {
      // Look for any child process.

      child = LIST_HEAD(&current->child_list);

      if (child == NULL && current != root_process) {
        err = -EINVAL;        
        goto exit;
      }

      found = false;

      while (child != NULL) {
        if (child->state == PROC_STATE_EXITED) {
          found = true;
          break;
        }

        child = LIST_NEXT(child, child_link);
      }
    } else {
      // Look for any child process with a specific process group id.
      // Check if any belongs to process group.

      child = LIST_HEAD(&current->child_list);

      if (child == NULL) {
        err = -EINVAL;
        goto exit;
      }

      found = false;
      found_in_pgrp = 0;

      while (child != NULL) {
        if (child->pgrp == -pid) {
          found_in_pgrp++;

          if (child->state == PROC_STATE_EXITED) {
            found = true;
            break;
          }
        }

        child = LIST_NEXT(child, child_link);
      }

      if (found_in_pgrp == 0) {
        err = -EINVAL;
        goto exit;
      }
    }

    if (!found && (options & WNOHANG)) {
        err = -EAGAIN;
        goto exit;
    } else if (!found) {
      TaskSleep(&current->child_list_rendez);
    }
  }

  if (!found) // && pending_signals
  {
    err = -EINTR;
    goto exit;
  }

  pid = get_process_pid(child);

  if (status != NULL) {
    current = get_current_process();

    if (CopyOut(status, &child->exit_status, sizeof *status) != 0) {
        err = -EFAULT;
        goto exit;
    }
  }

  LIST_REM_ENTRY(&current->child_list, child, child_link);  
  free_address_space(&child->as);
  
// FIXME: deactivate_pid(child->pid);
  free_process(child);
  
  return pid;

exit:
  return err;
}


/* @brief   Detach or free any child processes of a process
 *
 */
void detach_child_processes(struct Process *proc)
{
  struct Process *child;
  
  while ((child = LIST_HEAD(&proc->child_list)) != NULL) {
    LIST_REM_HEAD(&proc->child_list, child_link);

    if (child->state == PROC_STATE_EXITED) {
      free_address_space(&child->as);
      free_process(child);
    } else {
      LIST_ADD_TAIL(&root_process->child_list, child, child_link);
      child->parent = root_process;
    }
  }
}


/* @brief   Create a new process
 *
 * The address space created only has the kernel mapped. User-Space is marked as free.
 */
struct Process *do_create_process(void (*entry)(void *), void *arg, int policy, int priority,
                               bits32_t flags, char *basename, struct CPU *cpu)
{
  struct Process *current_proc;
  struct Process *new_proc;
  struct Thread *thread;
  pid_t pid;
  
  current_proc = get_current_process();

  Info ("do_create_process..");
  
  if ((new_proc = alloc_process(current_proc, flags, basename)) == NULL) {
    Info("fork alloc_process failed");
    return NULL;
  }

  init_ids(new_proc);
  init_fproc(new_proc);
  init_signals(new_proc);

  if (create_address_space(&new_proc->as) != 0) {
    Error("pmap_create failed");
    free_pid(pid);
    free_process_struct(new_proc);
    return NULL;
  }
  
  thread = do_create_thread(new_proc, entry, arg, SCHED_RR, 16, THREADF_USER, cpu);

  thread_start(thread);
  return new_proc;
}



/* @brief   Allocate a process structure
 *
 * @return  Allocated and initialized Process structure or NULL
 */
struct Process *alloc_process(struct Process *parent, uint32_t flags, char *name)
{
  struct Process *proc = NULL;
  pid_t pid;
    
  proc = alloc_process_struct();
  
  if (proc == NULL) {
    return NULL;
  }
  
  pid = alloc_pid(PIDF_PROCESS, (void *)proc);

  if (pid < 0) {
    free_process_struct(proc);
    return NULL;
  }  
    
  if (name != NULL) {
    strncpy(proc->basename, name, PROC_BASENAME_SZ-1);
  } else {
    proc->basename[0] = '\0';
  }
  
  proc->pid = pid;
  proc->parent = parent;
  
  if (parent != NULL) {
    LIST_ADD_TAIL(&parent->child_list, proc, child_link);
  }
  
  proc->state = PROC_STATE_INIT;
  proc->exit_status = 0;
  
  proc->flags = 0;

  InitRendez(&proc->rendez);
  InitRendez(&proc->child_list_rendez);
  InitRendez(&proc->thread_list_rendez);

  LIST_INIT(&proc->child_list);
  LIST_INIT(&proc->thread_list);
    
  return proc;
}


/* @brief   Free a process structure
 *
 * @param   proc, Process structure to free
 */
void free_process(struct Process *proc)
{
  struct Process *parent;

  free_pid(proc->pid);
  parent = proc->parent;

  if (parent != NULL) {
    LIST_REM_ENTRY(&parent->child_list, proc, child_link);
    proc->parent = NULL;
  }
  
  free_process_struct(proc);
}


/* @brief   Allocate a struct Process and clear it
 *
 * @return  Allocated process structure or NULL on error
 */
struct Process *alloc_process_struct(void)
{
  struct Process *proc;

  proc = LIST_HEAD(&free_process_list);
  
  if (proc == NULL) {
    return NULL;
  }  
  
  LIST_REM_HEAD(&free_process_list, free_link);
  
  memset(proc, 0, sizeof *proc);

  proc->canary1 = 0xcafef00d;
  proc->canary2 = 0xdeadbeef;

  return proc;
}


/* @brief   Free a struct Process
 *
 * @param   proc, pointer to process to free
 */
void free_process_struct(struct Process *proc)
{
  memset(proc, 0, sizeof *proc);
  proc->state = PROC_STATE_FREE;
  LIST_ADD_TAIL(&free_process_list, proc, free_link);
}


/* @brief   Check if a process is allowed to perform privileged I/O operations
 *
 * @param   proc, process to check
 * @return  true if it has privileges to perform I/O operations, false otherwise
 */
bool io_allowed(struct Process *proc)
{
#if 1
  return true;
#else
  // FIXME: check PROCF_ALLOW_IO
  if(proc->flags & PROCF_ALLOW_IO) {
    return true;
  } else {
    return false;
  }
#endif
}

