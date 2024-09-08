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
 * Signal handling syscalls and functions. Copied from Kielder's sources.
 * TODO: Update signal handling to work on ARM processor.  We need the
 * libc code updating to handle signals too.
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
	for (int t=1; t <= NSIG; t++) {
		dst->signal.handler[t-1] = src->signal.handler[t-1];
		dst->signal.handler_mask[t-1] = src->signal.handler_mask[t-1];		
		dst->signal.si_code[t-1] = 0;
	}
	
	dst->signal.sig_info      = src->signal.sig_info;
	dst->signal.sig_mask      = src->signal.sig_mask;
	dst->signal.sig_pending   = 0;
	dst->signal.sig_resethand = src->signal.sig_resethand;
	dst->signal.sig_nodefer   = src->signal.sig_nodefer;
	dst->signal.restorer    = src->signal.restorer;	
	dst->signal.sigsuspend_oldmask = 0;
	dst->signal.use_sigsuspend_mask = false;
	dst->signal.sigreturn_sigframe = NULL;
}


/* @brief   Initialize the signal handlers of a process
 */
void init_signals(struct Process *dst)
{
	for (int t=1; t <= NSIG; t++) {
		dst->signal.handler[t-1] = SIG_DFL;
		dst->signal.handler_mask[t-1] = 0x00000000;		
		dst->signal.si_code[t-1] = 0;
	}
	
	dst->signal.sig_info      = 0x00000000;
	dst->signal.sig_mask      = 0x00000000;
	dst->signal.sig_pending   = 0x00000000;
	dst->signal.sig_resethand = 0x00000000;
	dst->signal.sig_nodefer   = 0x00000000;
	dst->signal.restorer    = NULL;
	dst->signal.sigsuspend_oldmask = 0;
	dst->signal.use_sigsuspend_mask = false;	
	dst->signal.sigreturn_sigframe = NULL;
}


/* @brief   Update signal handlers due to a process exec'ing
 *
 */
void exec_signals(struct Process *dst)
{
	for (int t=1; t <= NSIG; t++) {
		if (dst->signal.handler[t-1] != SIG_IGN) {
			dst->signal.handler[t-1] = SIG_DFL;
    }
    
		dst->signal.handler_mask[t-1] = 0x00000000;
		dst->signal.si_code[t-1] = 0;		
	}
	
	dst->signal.sig_info      = 0x00000000;
	dst->signal.sig_resethand = 0x00000000;
	dst->signal.sig_nodefer   = 0x00000000;
	dst->signal.restorer    = NULL;
	dst->signal.sigsuspend_oldmask = 0;
	dst->signal.use_sigsuspend_mask = false;	
	dst->signal.sigreturn_sigframe = NULL;
}


/* @brief   Configure signal handling of a process
 *
 * TODO: Add sa_restorer pointer for trampoline address and
 * SA_RESTORER flag to initialize it.
 * current_process->signal.trampoline = act_in->sa_restorer.
 */
int sys_sigaction(int signal, const struct sigaction *act_in, struct sigaction *oact_out)
{
	struct sigaction act, oact;
	struct Process *current;
	
	Info("sys_sigaction(signal:%d)", signal);
	
	current = get_current_process();	

	if (signal <= 0 || signal >= NSIG) {        // FIXME  > NSIG
  	Info("sys_sigaction -EINVAL");
	  return -EINVAL;
	}
	
	if (signal == SIGKILL || signal == SIGSTOP) {
  	Error("sys_sigaction -EINVAL SIGKILL | SIGSTOP");
		return -EINVAL;
	}
	
	if (oact_out != NULL) {
		if (current->signal.sig_info &= SIGBIT(signal))
			oact.sa_flags |= SA_SIGINFO;
			
		oact._signal_handlers._handler = (void *) current->signal.handler[signal-1];
		oact.sa_mask = current->signal.handler_mask[signal-1];

		if (CopyOut (oact_out, &oact, sizeof (struct sigaction)) != 0) {
    	Info("sys_sigaction -EFAULT act_out");
		  return -EFAULT;
		}
	}
	
	if (act_in != NULL) {
		if (CopyIn(&act, act_in, sizeof (struct sigaction)) != 0) {
    	Info("sys_sigaction -FAULT act_in");
			return -EFAULT;
    }

		if (act.sa_flags & SA_SIGINFO) {
		  Info("sys_sigaction sa_flags set SA_SIGINFO");
			current->signal.sig_info |= SIGBIT(signal);
		}
		
		if (act.sa_flags & SA_NODEFER) {
		  Info("sys_sigaction sa_flags set SA_NODEFER");
			current->signal.sig_nodefer |= SIGBIT(signal);
		}
    
		if (act.sa_flags & SA_RESETHAND) {
		  Info("sys_sigaction sa_flags set SA_RESETHAND");
			current->signal.sig_resethand |= SIGBIT(signal);
		}

    // FIXME: Ummm, when did we add this?  Rename to sa_restorer
    if (act.sa_flags & SA_RESTORER) {
		  Info("sys_sigaction sa_flags set SA_RESTORER");
      current->signal.restorer = (void (*)(void)) act.sa_restorer;
    }

    current->signal.handler[signal-1] = (void *) act._signal_handlers._handler;
    current->signal.handler_mask[signal-1] = act.sa_mask;
    current->signal.handler_mask[signal-1] &= ~(SIGBIT(SIGKILL) | SIGBIT(SIGSTOP));


    Info("handler set to: %08x", (uint32_t)current->signal.handler[signal-1]);
    Info("mask for signal: %d is %08x", signal, current->signal.handler_mask[signal-1]);
	}
	
	return 0;
}


/* @brief   Send a signal to a process
 *
 * @param   pid, ID of target process to send signal to 
 * @param   signal, signal number to send to target process
 * @return  0 on success, negative errno on failure
 */
int sys_kill(int pid, int signal)
{
	Info("sys_kill(pid:%d, signal:%d)", pid, signal);
    
	if (signal <= 0 || signal > NSIG || pid == 0) {
	  return -EINVAL;
	}
	
	if (pid > 0) {
	  return do_kill_process(pid, signal);	
	}	else {
		return do_kill_process_group(-pid, signal);
	}

	return 0;
}


/*
 *
 */
int do_kill_process(pid_t pid, int signal)
{
	struct Process *proc;
	struct Thread *thread;
	int_state_t int_state;

	if (pid < 0 || pid >= max_process) {
	  return -EINVAL;
	}
	  
	proc = get_process(pid);
  
  if (proc == NULL) {
		return -EINVAL;
	}


  do_signal_process(proc, signal);
  
//	int_state = DisableInterrupts();
	
	if (proc->signal.handler[signal-1] != SIG_IGN) {
		proc->signal.sig_pending |= SIGBIT (signal);
		proc->signal.si_code[signal-1] = SI_USER;
  
//    thread = LIST_HEAD(&proc->thread_list);   // FIXME: Doe we need to signal all threads in a process?
                                                // FIXME: What about sigkill ?
//    TaskWakeupSpecific(thread, INTRF_SIGNAL);
	}
	
//	RestoreInterrupts(int_state);
  return 0;
}


/*
 *
 */
int do_kill_process_group(pid_t pgrp, int signal)
{
	struct Process *proc;
	struct Thread *thread;
	struct PidDesc *pd;
	int_state_t int_state;

	if (pgrp < 0 || pgrp >= max_process) {
	  return -EINVAL;
	}
	
	pd = pid_to_piddesc(pgrp);
	
	if (pd == NULL) {
	  return -EINVAL;
	}
		
	if ((pd->flags & PIDF_PGRP) == 0) {
	  return -EINVAL;
	}
	
	proc = LIST_HEAD(&pd->pgrp_list);
	
	while (proc != NULL) {	  
    do_signal_process(proc, signal);	  
	  proc = LIST_NEXT(proc, pgrp_link);
	}
	
	for (int t=0; t < max_process; t++) {
		proc = get_process(t);

    if (proc == NULL) {
        continue;
    }
		
	}

  return 0;
}


/*
 *
 */
int do_signal_process(struct Process *proc, int signal)
{
  struct Thread *thread;
  bool delivered = false;
    
	if (proc->signal.handler[signal-1] != SIG_IGN) {
		proc->signal.sig_pending |= SIGBIT(signal);
  	proc->signal.si_code[signal-1] = SI_USER;
    
    thread = LIST_HEAD(&proc->thread_list);

    while (thread != NULL) {
	    if (thread->state == THREAD_STATE_RENDEZ_BLOCKED &&
	        (SIGBIT(signal) & ~thread->signal.sig_mask) != 0) {	
        thread->signal.sig_pending = SIGBIT(signal);      
        TaskWakeupSpecific(thread, INTRF_SIGNAL);
        delivered = true;
        break;
      }
      
      thread = LIST_NEXT(thread, thread_link);
    }
  }
  
  return (delivered) ? 0 : -ENOENT;
}


/*
 *
 */
int do_signal_thread(struct Thread *thread, int signal)
{
  bool delivered = false;

  return (delivered) ? 0 : -ENOENT;
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
	struct Process *current;
	int_state_t int_state;
	
	current = get_current_process();
	
	if (CopyIn(&mask, mask_in, sizeof (sigset_t)) != 0) {
	  return -EFAULT;
	}
	
	int_state = DisableInterrupts();

	current->signal.sigsuspend_oldmask = current->signal.sig_mask;
	current->signal.use_sigsuspend_mask = true;
	
	current->signal.sig_mask = mask;

	if ((current->signal.sig_pending & ~current->signal.sig_mask) == 0) {
	  TaskSleep(&current->rendez);
	}
	
	RestoreInterrupts(int_state);	
	return 0;
}


/*
 *
 */
int sys_sigprocmask(int how, const sigset_t *set_in, sigset_t *oset_out)
{
	sigset_t set;
	struct Process *current;
	
	current = get_current_process();
	
	if (oset_out != NULL) {
		if (CopyOut (oset_out, &current->signal.sig_mask, sizeof (sigset_t)) != 0) {
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
		current->signal.sig_mask = set;
	} else if (how == SIG_BLOCK) {
		current->signal.sig_mask |= set;
  } else if (how == SIG_UNBLOCK) {
		current->signal.sig_mask &= ~set;
  } else {
		return -1;
	}
	
	return 0;
}


/* @brief   Return the set of pending signals
 * 
 * @param   set_out, Address to store the set of pending signals
 * @return  0 on success, negative errno on failure
 */
int sys_sigpending(sigset_t *set_out)
{
	sigset_t set;
	struct Process *current;
	
	current = get_current_process();
	
	set = current->signal.sig_pending & ~current->signal.sig_mask;	

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

