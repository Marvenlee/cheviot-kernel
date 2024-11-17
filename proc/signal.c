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
#include <kernel/signal.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <signal.h>
#include <sys/privileges.h>


/* @brief   Define the default actions and properties of each signal.
 */
const uint32_t sigprop[NSIG] = {
	0,		                          /* unused            */						              
	SP_KILL,                        /* 1  [ 0] SIGHUP    */
	SP_KILL,                        /* 2  [ 1] SIGINT    */
	SP_KILL|SP_CORE,                /* 3  [ 2] SIGQUIT   */
	SP_KILL|SP_CORE|SP_NORESET,     /* 4  [ 3] SIGILL    */
	SP_KILL|SP_CORE|SP_NORESET,     /* 5  [ 4] SIGTRAP   */
	SP_KILL|SP_CORE,                /* 6  [ 5] SIGABRT   */
	SP_KILL|SP_CORE,                /* 7  [ 6] SIGEMT    */
	SP_KILL|SP_CORE,                /* 8  [ 7] SIGFPE    */
	SP_KILL|SP_CANTMASK,            /* 9  [ 8] SIGKILL   */
	SP_KILL|SP_CORE,                /* 10 [ 9] SIGBUS    */
	SP_KILL|SP_CORE,                /* 11 [10] SIGSEGV   */
	SP_KILL|SP_CORE,                /* 12 [11] SIGSYS    */
	SP_KILL,                        /* 13 [12] SIGPIPE   */
	SP_KILL,                        /* 14 [13] SIGALRM   */
	SP_KILL,                        /* 15 [14] SIGTERM   */
	0,                              /* 16 [15] SIGURG    */
	SP_STOP|SP_CANTMASK,            /* 17 [16] SIGSTOP   */
	SP_STOP|SP_TTYSTOP,             /* 18 [17] SIGTSTP   */
	SP_CONT,                        /* 19 [18] SIGCONT   */
	0,                              /* 20 [19] SIGCHLD   */
	SP_STOP|SP_TTYSTOP,             /* 21 [20] SIGTTIN   */
	SP_STOP|SP_TTYSTOP,             /* 22 [21] SIGTTOU   */
	0,                              /* 23 [22] SIGIO     */
	SP_KILL,                        /* 24 [23] SIGXCPU   */
	SP_KILL,                        /* 25 [24] SIGXFSZ   */
	SP_KILL,                        /* 26 [25] SIGVTALRM */
	SP_KILL,                        /* 27 [26] SIGPROF   */
	0,					                    /* 28 [27] SIGWINCH  */
	0,						                  /* 29 [28] SIGINFO   */
	SP_KILL,						            /* 30 [29] SIGUSR1   */
	SP_KILL,						            /* 31 [30] SIGUSR2   */
};


/* @brief   Exit a process due to a signal
 */
void sig_exit(int signal)
{
  Info("sig_exit(signal:%d)", signal);
  sys_exit(signal<<8);
}


/* @brief   Copy signal handler settings to a new process
 */ 
void fork_signals(struct Process *dst, struct Process *src)
{
	for (int t=0; t < NSIG; t++) {
		dst->signal.handler[t] = src->signal.handler[t];
		dst->signal.handler_mask[t] = src->signal.handler_mask[t];		
	}
	
	dst->signal.sig_info      = src->signal.sig_info;
	dst->signal.sig_resethand = src->signal.sig_resethand;
	dst->signal.sig_nodefer   = src->signal.sig_nodefer;
	dst->signal.restorer      = src->signal.restorer;	
	dst->signal.sig_pending   = 0x00000000;

}


/* @brief   Initialize the signal handlers of a process
 */
void init_signals(struct Process *dst)
{
	for (int t=0; t < NSIG; t++) {
		dst->signal.handler[t] = SIG_DFL;
		dst->signal.handler_mask[t] = 0x00000000;
	}
	
	dst->signal.sig_info      = 0x00000000;
	dst->signal.sig_resethand = 0x00000000;
	dst->signal.sig_nodefer   = 0x00000000;
	dst->signal.restorer      = NULL;
	dst->signal.sig_pending   = 0x00000000;
}


/* @brief   Update signal handlers due to a process exec'ing
 *
 */
void exec_signals(struct Process *dst, struct Thread *dst_thread)
{
	for (int t=0; t < NSIG; t++) {
		if (dst->signal.handler[t] != SIG_IGN) {
			dst->signal.handler[t] = SIG_DFL;
    }
    
		dst->signal.handler_mask[t]     = 0x00000000;		
		dst_thread->signal.si_code[t]   = 0;		
		dst_thread->signal.si_value[t]  = 0;		
	}
	
	dst->signal.sig_info      = 0x00000000;
	dst->signal.sig_resethand = 0x00000000;
	dst->signal.sig_nodefer   = 0x00000000;
	dst->signal.restorer      = NULL;
	dst->signal.sig_pending   = 0x00000000;

	dst_thread->signal.sig_mask             = 0x00000000;
	dst_thread->signal.sig_pending          = 0x00000000;	
	dst_thread->signal.sigsuspend_oldmask   = 0x00000000;
	dst_thread->signal.use_sigsuspend_mask  = false;	
	dst_thread->signal.sigreturn_sigframe   = NULL;
}


/* @brief   Configure signal handling of a process
 *
 */
int sys_sigaction(int signal, const struct sigaction *act_in, struct sigaction *oact_out)
{
	struct sigaction act, oact;
	struct Process *cproc;
	
	Info("sys_sigaction(signal:%d)", signal);
	
	cproc = get_current_process();	

	if (signal <= 0 || signal > NSIG) {
  	Info("sys_sigaction -EINVAL");
	  return -EINVAL;
	}
	
	if (signal == SIGKILL || signal == SIGSTOP) {
  	Error("sys_sigaction -EINVAL SIGKILL | SIGSTOP");
		return -EINVAL;
	}
	
	if (oact_out != NULL) {
		if (cproc->signal.sig_info &= SIGBIT(signal))
			oact.sa_flags |= SA_SIGINFO;
			
		oact._signal_handlers._handler = (void *) cproc->signal.handler[signal-1];
		oact.sa_mask = cproc->signal.handler_mask[signal-1];

		if (CopyOut (oact_out, &oact, sizeof (struct sigaction)) != 0) {
    	Info("sys_sigaction -EFAULT act_out");
		  return -EFAULT;
		}
	}
	
	if (act_in != NULL) {
		if (CopyIn(&act, act_in, sizeof (struct sigaction)) != 0) {
    	Info("sys_sigaction -EFAULT act_in");
			return -EFAULT;
    }

		if (act.sa_flags & SA_SIGINFO) {
			cproc->signal.sig_info |= SIGBIT(signal);
		}
		
		if (act.sa_flags & SA_NODEFER) {
			cproc->signal.sig_nodefer |= SIGBIT(signal);
		}
    
		if (act.sa_flags & SA_RESETHAND) {
			cproc->signal.sig_resethand |= SIGBIT(signal);
		}

    if (act.sa_flags & SA_RESTORER) {
      cproc->signal.restorer = (void (*)(void)) act.sa_restorer;
    }

    cproc->signal.handler[signal-1] = (void *) act._signal_handlers._handler;
    cproc->signal.handler_mask[signal-1] = act.sa_mask;
    cproc->signal.handler_mask[signal-1] &= ~(SIGBIT(SIGKILL) | SIGBIT(SIGSTOP));
	}
	
	return 0;
}


/* @brief   Send a signal to a process
 *
 * @param   pid, ID of target process to send signal to 
 * @param   signal, signal number to send to target process
 * @return  0 on success, negative errno on failure
 */
int sys_kill(pid_t pid, int signal)
{
	Info("sys_kill(pid:%d, signal:%d)", pid, signal);
    
	if (signal <= 0 || signal > NSIG || pid == 0) {
	  Error("kill -EINVAL signal out of range");
	  return -EINVAL;
	}
	
	if (pid > 0) {
	  return do_kill_process(pid, signal, SI_USER, 0);	
	}	else {
		return do_kill_process_group(-pid, signal, SI_USER, 0);
	}

	return 0;
}


/* @brief   Send a signal to a thread
 *
 * @param   tid, ID of target thread to send signal to 
 * @param   signal, signal number to send to target process
 * @return  0 on success, negative errno on failure
 */
int sys_thread_kill(pid_t tid, int signal)
{
  // TODO: do_kill_thread(thread, signal);
}


/*
 *
 */
int do_kill_process(pid_t pid, int signal, int code, intptr_t val)
{
  struct Process *cproc;
	struct Process *proc;

  Info("do_kill_process(%d, %d)", pid, signal);

  cproc = get_current_process();
	proc = get_process(pid);
  
  if (proc == NULL) {
		return -ESRCH;
	}

  if (proc->uid != 0 && proc->uid != cproc->uid) {
    return -EPERM;
  }

  do_signal_process(proc, signal, code, val);
  return 0;
}


/*
 *
 */
int do_kill_process_group(pid_t pgid, int signal, int code, intptr_t val)
{
	struct Process *proc;
  struct Pgrp *pgrp;
  
  Info("do_kill_process_group(%d, %d)", pgid, signal);

	if (pgid < 0 || pgid >= max_process) {
    Info("pgrp out of range");
	  return -EINVAL;
	}
	
	pgrp = get_pgrp(pgid);
	
	if (pgrp == NULL) {
	  Info ("pgrp does not exist");
	  return -EINVAL;
	}
	
	proc = LIST_HEAD(&pgrp->process_list);
	
	while (proc != NULL) {	  
    do_signal_process(proc, signal, code, val);
	  proc = LIST_NEXT(proc, pgrp_link);
	}
	
  return 0;
}


/*
 * TODO: do_kill_thread
 */
int do_kill_thread(struct Thread *thread, int signal)
{
  return 0;
}


/*
 *
 */
void do_signal_process(struct Process *proc, int signal, int code, intptr_t val)
{
  Info("do_signal_process(%08x, %d)", (uint32_t)proc, signal);

  struct Thread *thread;
    
	if (proc->signal.handler[signal-1] != SIG_IGN) {                
    thread = LIST_HEAD(&proc->thread_list);

    // FIXME: What if all threads have their signal masked, where do we store the sig_pending, code and value?
    proc->signal.sig_pending |= SIGBIT(signal);

    while (thread != NULL) {
      if (SIGBIT(signal) & ~thread->signal.sig_mask) {
        proc->signal.sig_pending &= ~SIGBIT(signal);
        thread->signal.sig_pending |= SIGBIT(signal);
      	thread->signal.si_code[signal-1] = code;
      	thread->signal.si_value[signal-1] = val;        

	      if (thread->state == THREAD_STATE_RENDEZ_BLOCKED) {
          TaskWakeupSpecific(thread, INTRF_SIGNAL);
        }
        
        break;
      }
            
      thread = LIST_NEXT(thread, thread_link);
    }
  }
}


/*
 *
 */
void do_signal_thread(struct Thread *thread, int signal, int code, intptr_t val)
{
  struct Process *proc;
  
  proc = thread->process;
  
	if (proc->signal.handler[signal-1] != SIG_IGN) {
    thread->signal.sig_pending |= SIGBIT(signal);
  	thread->signal.si_code[signal-1] = code;
  	thread->signal.si_value[signal-1] = val;

    if (thread->state == THREAD_STATE_RENDEZ_BLOCKED &&
        (SIGBIT(signal) & ~thread->signal.sig_mask) != 0) {	
      TaskWakeupSpecific(thread, INTRF_SIGNAL);
    }
  }
}


/* @brief   Wait for a signal to occur.
 *
 * @param   mask_in, controls which signals are to be accepted (0) or ignored (1).
 * @return  0 on success, negative errno on failrure
 */
int sys_sigsuspend(const sigset_t *mask_in)
{
	sigset_t mask;
	uint32_t intstate;
	struct Thread *cthread;
	int_state_t int_state;
	
	cthread = get_current_thread();
	
	if (CopyIn(&mask, mask_in, sizeof (sigset_t)) != 0) {
	  return -EFAULT;
	}
	
	int_state = DisableInterrupts();

	cthread->signal.sigsuspend_oldmask = cthread->signal.sig_mask;
	cthread->signal.use_sigsuspend_mask = true;
	
	cthread->signal.sig_mask = mask;

	if ((cthread->signal.sig_pending & ~cthread->signal.sig_mask) == 0) {
	  TaskSleep(&cthread->rendez);
	}
	
	RestoreInterrupts(int_state);	
	return -EINTR;
}


/*
 * FIXME: Set thread's signal mask ?
 */
int sys_sigprocmask(int how, const sigset_t *set_in, sigset_t *oset_out)
{
	sigset_t set;
	struct Thread *cthread;
	
	cthread = get_current_thread();
	
	if (oset_out != NULL) {
		if (CopyOut (oset_out, &cthread->signal.sig_mask, sizeof (sigset_t)) != 0) {
		  return -EFAULT;
		}
  }
  
	if (set_in == NULL) {
	  return 0;
	}
	
	if (CopyIn (&set, set_in, sizeof (sigset_t)) == 0) {
    return -EFAULT;
	}
	
	if (how == SIG_SETMASK) {
		cthread->signal.sig_mask = set;
	} else if (how == SIG_BLOCK) {
		cthread->signal.sig_mask |= set;
  } else if (how == SIG_UNBLOCK) {
		cthread->signal.sig_mask &= ~set;
  } else {
		return -EINVAL;
	}
	
	return 0;
}


/* @brief   Return the set of pending signals
 * 
 * @param   set_out, Address to store the set of pending signals
 * @return  0 on success, negative errno on failure
 *
 * FIXME: This is pending signals to a thread
 */
int sys_sigpending(sigset_t *set_out)
{
	sigset_t set;
	struct Thread *cthread;
	
	cthread = get_current_thread();
	
	set = cthread->signal.sig_pending & ~cthread->signal.sig_mask;	

	if (CopyOut(set_out, &set, sizeof (sigset_t)) != 0) {
	  return -EFAULT;
	}
	
	return 0;
}


/* @brief   Perform the default action of a received signal
 */ 
void do_signal_default(int sig)
{
  Info("do_signal_default(%d)", sig);
  
	if (sigprop[sig] & SP_KILL) {
		sig_exit(sig);
	}
	
	
}


/* @brief   Pick a single signal from a set of signals
 *
 */
int pick_signal(uint32_t sigbits)
{
	int sig;

  if (sigbits == 0) {
    return 0;
  }

  sig = 32;
  
  if ((sigbits & 0xFFFF0000) == 0) {
     sig -= 16; 
     sigbits <<= 16;
  }
  
  if ((sigbits & 0xFF000000) == 0) {
    sig -= 8;
    sigbits <<= 8;
  }
  
  if ((sigbits & 0xF0000000) == 0) {
    sig -= 4; 
    sigbits <<= 4;
  }
  
  if ((sigbits & 0xC0000000) == 0) { 
    sig -= 2;
    sigbits <<= 2;
  }
  
  if ((sigbits & 0x80000000) == 0) {
    sig -= 1;
  }  

  return sig;
}

