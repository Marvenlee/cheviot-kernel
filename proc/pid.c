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

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <sys/wait.h>
#include <string.h>



/* @brief   Get the process ID of the calling process.
 *
 */
pid_t sys_getpid(void)
{
  return get_current_pid();    
}


/* @brief   Get the process ID of the parent of the calling process
 *
 */
pid_t sys_getppid(void)
{
  struct Process *current = get_current_process();
  
  if (current->parent == NULL) {
    return -EINVAL;
  }
  
  return current->parent->pid;
}


/*
 *
 */
pid_t sys_thread_gettid(void)
{
  return get_current_tid(); 
}



/*
 *
 */
pid_t get_current_pid(void)
{
  struct Process *current;
  
  current = get_current_process();  

  if (current->canary1 != 0xcafef00d || current->canary2 != 0xdeadbeef) {
    Error("get_current_pid corrupt!");
    Error("pid = %d, %08x", current->pid, current->pid);
    Error("canary1 = %08x, should be 0xcafef00d", current->canary1);
    Error("canary2 = %08x, should be 0xdeadbeef", current->canary2);
    
    KernelPanic();
  }

  return current->pid;
}


/*
 *
 */
pid_t get_current_tid(void)
{
  struct Thread *current;
  
  current = get_current_thread();  

  if (current->canary1 != 0xcafefaad || current->canary2 != 0xdeadbaaf) {
    Error("get_current_tid corrupt!");
    Error("tid = %d, %08x", current->tid, current->tid);
    Error("canary1 = %08x, should be 0xcafefaad", current->canary1);
    Error("canary2 = %08x, should be 0xdeadbaaf", current->canary2);
    KernelPanic();
  }

  return current->tid;
}


/* @brief   Get the process structure of the calling process
 * 
 * @param   pid, process ID of process to lookup
 * @return  Pointer to looked up process or NULL if it doesn't exist.
 */
struct Process *get_process(pid_t pid)
{
  if (pid < 0 || pid >= max_process ||
      ((pid_table[pid].flags & (PIDF_PROCESS | PIDF_ACTIVE)) != (PIDF_PROCESS | PIDF_ACTIVE))) {
    return NULL;
  }

  struct Process *proc = (struct Process *)pid_table[pid].object;
  
  if (proc->canary1 != 0xcafef00d || proc->canary2 != 0xdeadbeef || proc->pid != pid) {
    Error("get_process  corrupt!");
    Error("pid = %d, proc:%08x, proc->pid %d", pid, proc, proc->pid);
    Error("canary1 = %08x, should be 0xcafef00d", proc->canary1);
    Error("canary2 = %08x, should be 0xdeadbeef", proc->canary2);
    KernelPanic();
  }
  
  return pid_table[pid].object;
}


/*
 * 
 * @return  Pointer to looked up thread or NULL if it doesn't exist.
 */
struct Thread *get_thread(pid_t tid)
{
  if (tid < 0 || tid >= max_process ||
      ((pid_table[tid].flags & (PIDF_THREAD | PIDF_ACTIVE)) != (PIDF_THREAD | PIDF_ACTIVE))) {
    return NULL;
  }

  struct Thread *thread = (struct Thread *)pid_table[tid].object;
  
  if (thread->canary1 != 0xcafefaad || thread->canary2 != 0xdeadbaaf || thread->tid != tid) {
    Error("get_thread  corrupt!");
    Error("tid = %d, thread:%08x, thread->tid %d", tid, thread, thread->tid);
    Error("canary1 = %08x, should be 0xcafefaad", thread->canary1);
    Error("canary2 = %08x, should be 0xdeadbaaf", thread->canary2);
    KernelPanic();
  }
  
  return pid_table[tid].object;
}

struct Process *get_thread_process(struct Thread *thread)
{
  return thread->process;
}


// TODO:  Add get_pgrp_desc and get_session_desc, both returning piddesc


/* @brief   Get the process ID of a process
 * 
 * @param   proc, Process structure to get PID of
 * @return  PID of process
 */
pid_t get_process_pid(struct Process *proc)
{
  if (proc->canary1 != 0xcafef00d || proc->canary2 != 0xdeadbeef) {
    Error("get_process_pid  corrupt!");
    Error("proc = %08x", (uint32_t)proc);
    Error("canary1 = %08x, should be 0xcafef00d", proc->canary1);
    Error("canary2 = %08x, should be 0xdeadbeef", proc->canary2);
    KernelPanic();
  }

  return proc->pid;
}

/* @brief   Get the process ID of a process
 * 
 * @param   proc, Process structure to get PID of
 * @return  PID of process
 */
pid_t get_thread_tid(struct Thread *thread)
{
  if (thread->canary1 != 0xcafefaad || thread->canary2 != 0xdeadbaaf) {
    Error("get_thread_tid  corrupt!");
    Error("proc = %08x", (uint32_t)thread);
    Error("canary1 = %08x, should be 0xcafefaad", thread->canary1);
    Error("canary2 = %08x, should be 0xdeadbaaf", thread->canary2);
    KernelPanic();
  }

  return thread->tid;
}


/*
 *
 */
struct PidDesc *get_piddesc(struct Process *proc)
{
  return &pid_table[proc->pid];
}


/*
 *
 */
struct PidDesc *pid_to_piddesc(pid_t pid)
{
  if (pid < 0 || pid >= max_pid) {
    return NULL;
  }
    
  return &pid_table[pid];
}


/*
 *
 */
pid_t piddesc_to_pid(struct PidDesc *pid)
{
  return pid - pid_table;
}


/*
 *
 */
pid_t alloc_pid(uint32_t flags, void *object)
{
  struct PidDesc *pd;
  
  pd = LIST_HEAD(&free_piddesc_list);
  
  if (pd == NULL) {
    return -ENOMEM;
  }
  
  LIST_REM_HEAD(&free_piddesc_list, free_link);

  memset (pd, 0, sizeof *pd);
  pd->reference_cnt = 1;
  pd->flags = flags;
  pd->object = object;
  
  return piddesc_to_pid(pd);
}


/*
 *
 */
void free_pid(pid_t pid)
{
  struct PidDesc *pd = pid_to_piddesc(pid);
  
  Info("free_pid(%d)", pid);
  
  if (pd == NULL) {
    KernelPanic();
  }
  
  pd->reference_cnt--;  
  pd->object = NULL;
  
  pd->flags &= ~(PIDF_PROCESS | PIDF_THREAD);
  
  if (pd->reference_cnt == 0) {
    memset(pd, 0, sizeof *pd);    
    LIST_ADD_TAIL(&free_piddesc_list, pd, free_link);    
  }
}


/*
 *
 */
void activate_pid(int pid)
{
  struct PidDesc *pd = pid_to_piddesc(pid);

  KASSERT(pd != NULL);
  
  pd->flags |= PIDF_ACTIVE;
}


/*
 *
 */
void deactivate_pid(int pid)
{
  struct PidDesc *pd = pid_to_piddesc(pid);
  
  KASSERT(pd != NULL);
    
  pd->flags &= ~PIDF_ACTIVE;
}


/*
 *
 */
int pgrp_add_proc(pid_t session_leader, pid_t pgrp, struct Process *proc)
{
  struct PidDesc *session_pd;
  struct PidDesc *pgrp_pd;

  Info("pgrp_add_proc");

  session_pd = pid_to_piddesc(proc->sid);
  pgrp_pd = pid_to_piddesc(proc->pgrp);
  
  if (session_pd != NULL) {    
    session_pd->reference_cnt++;
  }
  
  if (pgrp_pd != NULL) { 
    pgrp_pd->reference_cnt++;
    LIST_ADD_TAIL(&pgrp_pd->pgrp_list, proc, pgrp_link);
  }
      
  return 0;
}


/*
 *
 */
int pgrp_rem_proc(struct Process *proc)
{
  Info("pgrp_rem_proc");

  struct PidDesc *pgrp_pd;
  struct PidDesc *session_pd;

  pgrp_pd = pid_to_piddesc(proc->pgrp);
  
  if (pgrp_pd != NULL) {
    pgrp_pd->reference_cnt--;
    LIST_REM_ENTRY(&pgrp_pd->pgrp_list, proc, pgrp_link);
    
    if (pgrp_pd->reference_cnt == 0) {
      free_pid(proc->pgrp);
    }
  }

  proc->pgrp = INVALID_PID;

//  proc->fproc->controlling_tty = NULL;
//  proc->fproc->tty_pgrp = -1;

  session_pd = pid_to_piddesc(proc->sid);

  if (session_pd != NULL) {    
    session_pd->reference_cnt--;
    
    if (session_pd->reference_cnt == 0) {
      free_pid(proc->sid);
    }
  }

  proc->sid = INVALID_PID;  
  proc->session_leader = false;
}





