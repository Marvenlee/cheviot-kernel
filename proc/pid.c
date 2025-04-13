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
 *
 * --
 * Process, thread, process group and session ID management
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



/* @brief   Get the process ID of the calling thread
 *
 */
pid_t sys_getpid(void)
{
  return get_current_pid();    
}


/* @brief   Get the process ID of the parent process of the calling thread's process
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


/* @brief   Get the thread ID of the calling thread
 *
 */
pid_t sys_thread_gettid(void)
{
  return get_current_tid(); 
}


/* @brief   Get the current process's session ID
 *
 */
int sys_getsid(pid_t pid)
{
  struct Process *current = get_current_process();
  struct Process *proc;

  if (pid == 0) {
    proc = current;
  } else {
    proc = get_process(pid);
  }
    
  if (proc == NULL) {
    return -ESRCH;
  }
  
  if (current->sid != proc->sid) {
    return -EPERM;
  }
    
  return proc->sid;
}


/* @brief   Create a new session.
 *
 * Creates a new session if the calling process is not a process group leader.
 * 
 * The calling process becomes the leader of the new session and the process group
 * leader of a new process group within the session.
 */

int sys_setsid(void)
{
  struct Process *current = get_current_process();
  struct PidDesc *pd;
  struct Session *current_session;
  struct Session *new_session;
  
  Info("sys_setsid()");

  if (current->sid == current->pid) {
    Error("sys_setsid() -EPERM sid = pid");
    return -EPERM;
  }

	if (current->pgid == current->pid) {
    Error("sys_setsid() -EPERM pgid = pid");
	  return -EPERM;
  }	  
	
	current_session = get_session(current->sid);
	
	if (current_session != NULL) {
	  Info("current session exists, removing from pgrp and session");
	  remove_from_pgrp(current);
	  remove_from_session(current);
  }
	
	new_session = alloc_session();
	
	if (new_session == NULL) {
	  // This shouldn't be possible
    Error("sys_setsid() -ENOMEM");
	  return -ENOMEM;
	}
	
	new_session->sid = current->pid;
  LIST_ADD_TAIL(&new_session->process_list, current, session_link);
  
	current->sid = new_session->sid;
  current->pgid = INVALID_PID;
  
	pd = pid_to_piddesc(current->sid);
	
	pd->session = new_session;
	pd->pgrp = NULL;
	
  return current->sid;
}


/* @brief   Return the process group ID of the process whose process ID is equal to pid.
 *
 * If pid is equal to 0 then it returns the process group ID of the calling process.
 */
pid_t sys_getpgid(pid_t pid)
{
  struct Process *current = get_current_process();
  struct Process *proc;

  if (pid == 0) {
    proc = current;
  } else {
    proc = get_process(pid);
  }
    
  if (proc == NULL) {
    return -ESRCH;
  }
  
  if (current->sid != proc->sid) {
    return -EPERM;
  }

  return proc->pgid;
}


/* @brief   Set the process group ID of a process whose process ID is equal to pid.
 *
 * The process either joins an existing process group or create a new process group
 * within the session of the calling process.
 *
 * If pid is 0, the process ID of the calling process is used. 
 * If pgid is 0, the process ID of the indicated process is used.
 */
int sys_setpgid(pid_t pid, pid_t pgid)
{
  struct Process *current = get_current_process();
  struct Process *proc;
  struct Pgrp *pgrp;
  
  Info("sys_setpgid(pid:%d, pgid:%d)", pid, pgid);
  
  if (pid == 0) {
    proc = current;
  } else {
    proc = get_process(pid);
  }
    
  if (proc == NULL) {
    Info("pgid not found");
    return -ESRCH;
  }
  
  if (current->sid != proc->sid) {
    Info("current sid != proc sid");
    return -EPERM;
  }

  pgrp = get_pgrp(pgid);

  if (pgrp == NULL) {
    Info("pgrp not set");
    return -EPERM;
  }
  
  if (pgrp->sid != current->sid) {
    Info("current sid != proc sid");
    return -EPERM;
  }
  
  proc->pgid = pgid;
  return 0;
}
 

/*
 *
 */
pid_t sys_getpgrp(void)
{
  struct Process *current = get_current_process();

  return current->pgid;
}


/*
 *
 */
int sys_setpgrp(void)
{
  struct Process *current = get_current_process();
  struct PidDesc *pd;
  struct Pgrp *new_pgrp;
  
  Info("sys_setpgrp()");
  
  if (current->pgid == current->pid) {
    Error("sys_setpgrp() -EPERM pgid = pid");
    return -EINVAL;
  }

  pd = get_piddesc(current);
  
  if (current->pgid != INVALID_PID) {
    Info("remove from existing pgrp");
    remove_from_pgrp(current);    // this will set current_pgrp_pd->pgrp to NULL
  }
  
  current->pgid = current->pid;
  
  new_pgrp = alloc_pgrp();
  
  if (new_pgrp == NULL) {
    // shouldn't happen
    Error("sys_setpgrp() -ENOMEM");
    return -ENOMEM;
  }
  
  new_pgrp->sid = current->sid;
  LIST_ADD_TAIL(&new_pgrp->process_list, current, pgrp_link);

  pd->pgrp = new_pgrp; 
  return 0;
}


/*
 *
 */
pid_t get_current_pid(void)
{
  struct Process *current;
  
  current = get_current_process();
  return current->pid;
}


/*
 *
 */
pid_t get_current_tid(void)
{
  struct Thread *current;
  
  current = get_current_thread();
  return current->tid;
}


/* @brief   Get the process structure of the calling process
 * 
 * @param   pid, process ID of process to lookup
 * @return  Pointer to looked up process or NULL if it doesn't exist.
 */
struct Process *get_process(pid_t pid)
{
  if (pid <= 0 || pid >= max_pid) {
    return NULL;
  }

  return pid_table[pid - 1].proc;
}


/*
 * 
 * @return  Pointer to looked up thread or NULL if it doesn't exist.
 */
struct Thread *get_thread(pid_t tid)
{
  if (tid <= 0 || tid >= max_pid) {
    return NULL;
  }

  return pid_table[tid - 1].thread;
}

struct Process *get_thread_process(struct Thread *thread)
{
  return thread->process;
}


/* @brief   Get the process ID of a process
 * 
 * @param   proc, Process structure to get PID of
 * @return  PID of process
 */
pid_t get_process_pid(struct Process *proc)
{
  return proc->pid;
}

/* @brief   Get the process ID of a process
 * 
 * @param   proc, Process structure to get PID of
 * @return  PID of process
 */
pid_t get_thread_tid(struct Thread *thread)
{
  return thread->tid;
}


/*
 *
 */
struct PidDesc *get_piddesc(struct Process *proc)
{
  return &pid_table[proc->pid - 1];
}


/*
 *
 */
struct PidDesc *pid_to_piddesc(pid_t pid)
{
  if (pid <= 0 || pid >= max_pid) {
    return NULL;
  }
    
  return &pid_table[pid - 1];
}


/*
 *
 */
pid_t piddesc_to_pid(struct PidDesc *piddesc)
{
  return piddesc - pid_table + 1;
}


/*
 *
 */
pid_t alloc_pid_proc(struct Process *proc)
{
  struct PidDesc *pd;
  
  pd = LIST_HEAD(&free_piddesc_list);
  
  if (pd == NULL) {
    return -ENOMEM;
  }
  
  LIST_REM_HEAD(&free_piddesc_list, free_link);

  memset (pd, 0, sizeof *pd);
  pd->proc = proc;
  return piddesc_to_pid(pd);
}



/*
 *
 */
pid_t alloc_pid_thread(struct Thread *thread)
{
  struct PidDesc *pd;
  
  pd = LIST_HEAD(&free_piddesc_list);
  
  if (pd == NULL) {
    return -ENOMEM;
  }
  
  LIST_REM_HEAD(&free_piddesc_list, free_link);

  memset (pd, 0, sizeof *pd);
  pd->thread = thread;
  return piddesc_to_pid(pd);
}



/* @brief   Free a PID/TID
 *
 * TODO: If a session leader dies, we need to signal the foreground process group to terminate.
 */
void free_pid(pid_t pid)
{
  struct PidDesc *pd = pid_to_piddesc(pid);
  struct Session *session;
  struct Pgrp *pgrp;

  if (pd == NULL) {
    KernelPanic();
  }

  pd->proc = NULL;
  pd->thread = NULL;
    
  session = pd->session;
  pgrp = pd->pgrp;
  
    
  if (session != NULL && LIST_EMPTY(&session->process_list)) {
    free_session(session);
    pd->session = NULL;
  }
  
  if (pgrp != NULL && LIST_EMPTY(&pgrp->process_list)) {
    free_pgrp(pgrp);
    pd->pgrp = NULL;
  }

  if (pd->session == NULL && pd->pgrp == NULL) {
    LIST_ADD_TAIL(&free_piddesc_list, pd, free_link);    
  }
}


/*
 *
 */
void init_session_pgrp(struct Process *proc)
{
  proc->pgid = INVALID_PID;
  proc->sid  = INVALID_PID;
}


/*
 *
 */
void fork_session_pgrp(struct Process *new_proc, struct Process *old_proc)
{
  struct Session *session;
  struct Pgrp *pgrp;
  
  new_proc->sid  = old_proc->sid;          // session this process belongs
  new_proc->pgid = old_proc->pgid;         // process group to which this process belongs

  session = get_session(new_proc->sid);
  pgrp = get_pgrp(new_proc->pgid);
  
  if (session != NULL) {
    LIST_ADD_TAIL(&session->process_list, new_proc, session_link);
  }
  
  if (pgrp != NULL) {
    LIST_ADD_TAIL(&pgrp->process_list, new_proc, pgrp_link);
  }
}


/*
 *
 */
void fini_session_pgrp(struct Process *proc)
{
  remove_from_pgrp(proc);
  remove_from_session(proc);
}


/*
 * TODO: Should map 1-to-1 with piddesc and pids
 * No need for allocation
 */
struct Session *alloc_session(void)
{
  struct Session *session;
  
  session = LIST_HEAD(&free_session_list);
  
  if (session != NULL) {
    LIST_REM_HEAD(&free_session_list, free_link);

    memset(session, 0, sizeof *session);
  	session->foreground_pgrp = INVALID_PID;
  	session->controlling_tty = NULL;
    LIST_INIT(&session->process_list);
  }
  
  return session;
}


/*
 *
 */
void free_session(struct Session *session)
{
  if (session->controlling_tty != NULL) {
    session->controlling_tty->tty_sid = INVALID_PID;    
  }
  
  LIST_ADD_TAIL(&free_session_list, session, free_link);
}


/*
 * TODO: Should map 1-to-1 with piddesc and pids
 * No need for allocation
 */
struct Pgrp *alloc_pgrp(void)
{
  struct Pgrp *pgrp;
  
  pgrp = LIST_HEAD(&free_pgrp_list);
  
  if (pgrp != NULL) {
    LIST_REM_HEAD(&free_pgrp_list, free_link);
    
    memset(pgrp, 0, sizeof *pgrp);
    LIST_INIT(&pgrp->process_list);
  }
  
  return pgrp;
}


/*
 *
 */
void free_pgrp(struct Pgrp *pgrp)
{
    LIST_ADD_TAIL(&free_pgrp_list, pgrp, free_link);
}



struct Session *get_session(pid_t sid)
{
  struct PidDesc *pd = pid_to_piddesc(sid);

  if (pd == NULL) {
    return NULL;
  }

  return pd->session;
}


struct Pgrp *get_pgrp(pid_t pgid)
{
  struct PidDesc *pd = pid_to_piddesc(pgid);

  if (pd == NULL) {
    return NULL;
  }

  return pd->pgrp;
}




void remove_from_pgrp(struct Process *proc)
{
  struct PidDesc *pgrp_pd;
  struct Pgrp *pgrp;
  struct Session *session;
  
  pgrp_pd = pid_to_piddesc(proc->pgid);
  
  if (pgrp_pd == NULL) {
    return;
  }
  
  pgrp = pgrp_pd->pgrp;
  
  if (pgrp == NULL) {
    return;
  }
  
  LIST_REM_ENTRY(&pgrp->process_list, proc, pgrp_link);

  session = get_session(pgrp->sid);

  if (LIST_EMPTY(&pgrp->process_list)) {
    free_pgrp(pgrp);
    pgrp_pd->pgrp = NULL;
  }
  
  if (session != NULL) {
    if (session->foreground_pgrp == proc->pgid) {
      session->foreground_pgrp = INVALID_PID;
    } 
  }

  free_pid(proc->pgid);
    
  proc->pgid = INVALID_PID;
}


/*
 * this will set proc_session_pd->session to NULL
 */
void remove_from_session(struct Process *proc)
{
  struct PidDesc *session_pd;
  struct Session *session;
  
  session_pd = pid_to_piddesc(proc->sid);
  
  if (session_pd == NULL) {
    return;
  }
  
  session = session_pd->session;
  
  if (session == NULL) {
    return;
  }
  
  LIST_REM_ENTRY(&session->process_list, proc, session_link);

  if (LIST_EMPTY(&session->process_list)) {
    free_session(session);
    session_pd->session = NULL;
  }
      
  free_pid(proc->sid);

  proc->sid = INVALID_PID;

}



