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


/*
 * TODO: Add user-space "self/TLS" pointer as argument.
 * Move policy/priority into sched_params struct.
 * Remove basename.
 *
 * TODO: May need to inherit
 */
pid_t sys_thread_create(void (*entry)(void *), void *arg,
                                  int policy, int priority,
                                  uint32_t flags, char *basename)
{
  struct Process *current_proc;
  struct Thread *current_thread;
  struct Thread *thread;
  
  current_proc = get_current_process();
  current_thread = get_current_thread();
    
  thread = do_create_thread(current_proc, entry, arg,
                              policy, priority, flags, 
                              current_thread->signal.sig_mask, 
                              get_cpu());

  if (thread == NULL) {
    return -ENOMEM;
  }

  thread_start(thread);
  
  return get_thread_tid(thread);
}


/*
 * What if multiple threads try to join same thread?
 * Mark within target thread who is doing join.  Deny other threads from performing
 * join on thread.  thread->joiner = current_thread.
 */
int sys_thread_join(pid_t tid, intptr_t *_status)
{
  struct Thread *thread;
  intptr_t status;
  int sc;
  
  thread = get_thread(tid);
  
  if (thread == NULL) {
    return -ESRCH;
  }
  
  sc = do_join_thread(thread, &status);
  
  if (sc == 0) {
    CopyOut(_status, &status, sizeof status);
  }
  
  return sc;
}


/*
 *
 */
void sys_thread_exit(intptr_t exit_status)
{
  Error("sys_thread_exit()");
  
  do_exit_thread(exit_status);
}


/*
 *
 */
int sys_thread_cancel(pid_t tid)
{
  return -ENOSYS;
}


/*
 *
 */
void *sys_thread_self(void)
{
  // TODO: Return pointer set for user-mode thread structure
  return NULL;
}


/* @brief   Create the first thread in a forked process
 *
 * NOTE: The thread in the new process has a different thread-id (tid) to that in
 * the original process.  Any user-mode pthread_t structure must be updated to reflect
 * this difference.
 */
struct Thread *fork_thread(struct Process *new_proc, struct Process *old_proc, struct Thread *old_thread)
{
  struct Thread *thread;
  pid_t tid;
  void *stack;
  
  Info("fork_thread()");
  
  thread = alloc_thread_struct();

  if (thread == NULL) {
    return NULL;
  }

  tid = alloc_pid_thread(thread);
  
  if (tid < 0) {
    free_thread_struct(thread);  
    return NULL;
  }  

  stack = kmalloc_page();     // TODO: Add 4k alloc size, KERNEL_STACK_SZ
  
  if (stack == NULL) {
    free_pid(tid);
    free_thread_struct(thread);  
    return NULL;
  }
  
  
  init_thread(thread, get_cpu(), new_proc, stack, tid, 0x00000000);
  init_msgport(&thread->reply_port);
  dup_schedparams(thread, old_thread);

  arch_init_fork_thread(new_proc, old_proc, thread, old_thread);

  return thread;
}


/* @brief   Create a kernel task
 *
 */
struct Thread *create_kernel_thread(void (*entry)(void *), void *arg, int policy, int priority,
                               uint32_t flags, struct CPU *cpu)
{
  struct Thread *thread;

  flags |= THREADF_KERNEL;
    
  thread = do_create_thread(root_process, entry, arg, policy, priority, flags, 0x00000000, cpu);

  if (thread != NULL) {
    thread_start(thread);
  }
    
  return thread;
}


/*
 *
 */
struct Thread *do_create_thread(struct Process *new_proc, void (*entry)(void *), void *arg,
                                int policy, int priority, uint32_t flags, uint32_t sig_mask,
                                struct CPU *cpu)
{
  struct Thread *thread;
  pid_t tid;
  void *stack;
    
  Info("do_create_thread (new_proc:%08x, entry:%08x", (uint32_t)new_proc, (uint32_t)entry);
    
  thread = alloc_thread_struct();

  if (thread == NULL) {
    return NULL;
  }

  tid = alloc_pid_thread(thread);
  
  if (tid < 0) {
    free_thread_struct(thread);  
    return NULL;
  }  

  stack = kmalloc_page();     // TODO: Add 4k alloc size, KERNEL_STACK_SZ
  
  if (stack == NULL) {
    free_pid(tid);
    free_thread_struct(thread);  
    return NULL;
  }
  
  init_thread(thread, get_cpu(), new_proc, stack, tid, sig_mask);
  init_msgport(&thread->reply_port);
  init_schedparams(thread, policy, priority);

  if (flags & THREADF_KERNEL) {
    arch_init_kernel_thread(thread, entry, arg);
  } else {
    arch_init_user_thread(thread, entry, arg);
  }
  
  return thread;
}


/*
 *
 * Later add thread affinity mask and some load balancing.
 */
void init_thread(struct Thread *thread, struct CPU *cpu, struct Process *proc, void *stack,
                 pid_t tid, uint32_t sig_mask)
{
  InitRendez(&thread->rendez);

  LIST_ADD_TAIL(&proc->thread_list, thread, thread_link);

  thread->cpu = cpu;
  thread->stack = stack;
  thread->tid = tid;
  thread->process = proc;
  thread->joiner_thread = NULL;
  thread->state = THREAD_STATE_INIT;
  thread->blocking_rendez = NULL;
  thread->exit_status = 0;

  Info("init_thread thread:%08x, tid:%d, proc:%08x", (uint32_t)thread, tid, (uint32_t)proc);

  thread->intr_flags = 0;
  thread->kevent_event_mask = 0;
  thread->event_mask = 0;
  thread->pending_events = 0;
  thread->detached = false;

  thread->event_knote = NULL;
  thread->event_kqueue = NULL;
  LIST_INIT(&thread->knote_list);
  LIST_INIT(&thread->isr_handler_list);

  // TODO: init signal state
  thread->signal.sig_mask = sig_mask; 
  thread->signal.sig_pending = 0;
	thread->signal.sigsuspend_oldmask = 0;
	thread->signal.use_sigsuspend_mask = false;
	thread->signal.sigreturn_sigframe = NULL;

  for (int t=0; t<NSIG; t++) {
    thread->signal.si_code[t] = 0;
    thread->signal.si_value[t] = 0;
  }

  if (sig_mask != 0xFFFFFFFF) {
    LIST_ADD_TAIL(&proc->unmasked_signal_thread_list, thread, unmasked_signal_thread_link);
  }  
}


/*
 *
 */
void do_kill_other_threads_and_wait(struct Process *current, struct Thread *current_thread)
{
  struct Thread *thread;
  
  Info("signal all other threads to terminate");
  thread = LIST_HEAD(&current->thread_list);  

  while(thread != NULL) {
    if (thread != current_thread) {
      Info("kill other thread:%08x", (uint32_t)thread);
  
      thread->detached = true;        
      do_kill_thread(thread, SIGKILL);  // This should not block/yield otherwise thread list could be changed
                                        // perhaps add a busy condition variable around thread list.                                          
    }
    
    thread = LIST_NEXT(thread, thread_link);
  }

  // Wait until we are the last thread alive in the process

  while(LIST_HEAD(&current->thread_list) != current_thread &&
        LIST_TAIL(&current->thread_list) != current_thread) {
    TaskSleep(&current->thread_list_rendez);
  }
}

/*
 *
 */
int do_exit_thread(intptr_t status)
{
  struct Thread *thread;
  struct Process *proc;
  
  Error("do_exit_thread");
  proc = get_current_process();
  thread = get_current_thread();
      
  LIST_REM_ENTRY(&proc->thread_list, thread, thread_link);    

  if (LIST_EMPTY(&proc->thread_list)) {
    // We are the final thread, detach and notify the parent process to finish cleanup.    
    thread->detached = true;
    
    proc->state = PROC_STATE_EXITED;
    
    if (proc->parent != NULL) {
      // FIXME: sys_kill(proc->parent->pid, SIGCHLD);
      TaskWakeupAll(&proc->parent->child_list_rendez);
    }
  }
  if (thread->detached == true) {
    // Add thread to reaper's thread list
    thread->process = root_process;
    pmap_switch(thread->process, NULL);    
    LIST_ADD_TAIL(&thread_reaper_detached_thread_list, thread, thread_link);  
    TaskWakeup(&thread_reaper_rendez);
  } else {
    // Add thread back to process's thread list
    LIST_ADD_TAIL(&proc->thread_list, thread, thread_link);  
    TaskWakeupAll(&proc->thread_list_rendez);
  }
      
  do_free_all_isrhandlers(proc, thread);
  
  thread_stop();

  // Shouldn't get here  
  return 0;
}


/*
 *
 */
int do_join_thread(struct Thread *thread, intptr_t *status)
{
  struct Process *proc;
  struct Thread *current_thread;
  
  Info("do_join_thread");
  
  proc = get_current_process();
  current_thread = get_current_thread();

  // Prevent deadlock of two threads joining each other.  
  if (current_thread->joiner_thread == thread) {
    return -EDEADLK;
  }
  
  // Set the joiner_thread of terminating thread to let other threads doing a join
  // on it to fail.  Prevent other threads doing a join.
  if (thread->joiner_thread != NULL) {
    return -EBUSY;
  }
    
  thread->joiner_thread = current_thread;
  
  while (thread->state != THREAD_STATE_EXITED) {
    TaskSleep(&proc->thread_list_rendez);
  }
  
  // TODO: We might want to be interruptible, so if failed, set joiner thread back to NULL
  // TODO: May want to be sure thread hasn't exited, avoid race conditions.
  // TODO: Want to avoid if thread marks itself as detached.  Needs to wakeup this thread,
  // TODO: This thread then needs to check if the thread still exists on the thread list.  
  
  LIST_REM_ENTRY(&proc->thread_list, thread, thread_link);
  
  if (status != NULL) {
    *status = thread->exit_status;
  }
  
  kfree_page(thread->stack);    
  free_thread(thread);
    
  return 0;
} 


/*
 *
 */
void free_thread(struct Thread *thread)
{  
  free_pid(thread->tid);
  free_thread_struct(thread);
}


/*
 *
 */
struct Thread *alloc_thread_struct(void)
{
  struct Thread *thread;

  thread = LIST_HEAD(&free_thread_list);
  
  if (thread == NULL) {
    return NULL;
  }  
  
  LIST_REM_HEAD(&free_thread_list, free_link);
  
  memset(thread, 0, sizeof *thread);

  thread->canary1 = 0xcafefaad;
  thread->canary2 = 0xdeadbaaf;

  return thread;
}


/*
 *
 */
void free_thread_struct(struct Thread *thread)
{  
  memset(thread, 0, sizeof *thread);
  LIST_ADD_TAIL(&free_thread_list, thread, free_link);
}


/*
 * NOTE: Is any special handling needed for root process and stopping this thread?
 */ 
void thread_reaper_task(void *arg)
{
  (void *)arg;
  struct Thread *thread;
  struct Process *proc;

  while(1) {
    while((thread = LIST_HEAD(&thread_reaper_detached_thread_list)) == NULL) {
      TaskSleep(&thread_reaper_rendez);
    }

    LIST_REM_ENTRY(&thread_reaper_detached_thread_list, thread, thread_link);
    
    proc = thread->process;        
    kfree_page(thread->stack);
    free_thread(thread);
    
    // Wakeup anything waiting on proc thread lists (other proc/threads doing join
    TaskWakeupAll(&proc->thread_list_rendez);

  }  
}




