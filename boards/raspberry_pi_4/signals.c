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

#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/board/task.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/proc.h>
#include <kernel/signal.h>
#include <kernel/types.h>
#include <ucontext.h>


/* @brief   Restore context when returning from a signal handler
 *
 */
void sys_sigreturn(struct sigframe *u_sigframe)
{
	struct Thread *cthread;
	
	cthread = get_current_thread();	
  cthread->signal.sigreturn_sigframe = u_sigframe;
}


/* @brief   Check for signals prior to returning to user-space
 *
 * Called just before returning from a syscall, interrupt or exception.
 *
 * If we are returning from USigSuspend() then the process's current sigmask
 * will have been changed and the old mask saved.  We use the current sigmask
 * to determine what signal to deliver then reset it to the mask used prior
 * to calling USigSuspend().  See USigSuspend() for details.
 *
 * TODO: Copied from Kielder's x86 sources. Implement signal handling on ARM.
 */
void check_signals(struct UserContext *uc)
{
	struct sigframe sigframe;
	struct sigframe *u_sigframe;
	sigset_t caught_signals;
	sigset_t old_mask;
	int sig;
	struct Process *cproc;
	struct Thread *cthread;

	cproc = get_current_process();
	cthread = get_current_thread();

  if (cthread->signal.sigreturn_sigframe != NULL) {
    u_sigframe = cthread->signal.sigreturn_sigframe;
    cthread->signal.sigreturn_sigframe = NULL;

	  if (CopyIn (&sigframe, u_sigframe, sizeof (struct sigframe)) != 0) {	
      Error("Failed to copy in u_sigframe, sig_exit with -SIGSEGV");
		  sig_exit(SIGSEGV);
	  }
	  
    uc->sp = sigframe.sf_uc.uc_mcontext.sp;
    uc->lr = sigframe.sf_uc.uc_mcontext.lr;
    uc->cpsr = sigframe.sf_uc.uc_mcontext.cpsr;    // Should be svc_mode spsr  (Mask it?)
			    
    uc->r1 = sigframe.sf_uc.uc_mcontext.r1;
    uc->r2 = sigframe.sf_uc.uc_mcontext.r2;
    uc->r3 = sigframe.sf_uc.uc_mcontext.r3;
    uc->r4 = sigframe.sf_uc.uc_mcontext.r4;
    uc->r5 = sigframe.sf_uc.uc_mcontext.r5;
    uc->r6 = sigframe.sf_uc.uc_mcontext.r6;
    uc->r7 = sigframe.sf_uc.uc_mcontext.r7;
    uc->r8 = sigframe.sf_uc.uc_mcontext.r8;
    uc->r9 = sigframe.sf_uc.uc_mcontext.r9;
    uc->r10 = sigframe.sf_uc.uc_mcontext.r10;
    uc->r11 = sigframe.sf_uc.uc_mcontext.r11;
    uc->r12 = sigframe.sf_uc.uc_mcontext.r12;
    uc->r0 = sigframe.sf_uc.uc_mcontext.r0;
    uc->pc = sigframe.sf_uc.uc_mcontext.pc;        // Should be svc_mode LR register.

	  cthread->signal.sig_mask = sigframe.sf_uc.uc_sigmask;
  }

  // Check if we have any process directed signals.
  if (cproc->signal.sig_pending & ~cthread->signal.sig_mask) {
    cthread->signal.sig_pending |= (cproc->signal.sig_pending & ~cthread->signal.sig_mask);
    cproc->signal.sig_pending &= ~(cproc->signal.sig_pending & ~cthread->signal.sig_mask);

    // TODO: Clear any siginfo fields of these new signals. Unless we also store
    // the siginfo fields in the proc and copy those across.
  }

	caught_signals = cthread->signal.sig_pending & ~cthread->signal.sig_mask;
	
	
	if (caught_signals == 0) {
	  return;
	}		
	
//	Info("caught_signals: %08x", caught_signals);
		
  if (caught_signals & SIGBIT(SIGKILL)) {
    Info("SIGKILL received");
    sig_exit(SIGKILL);
  }
    
  if (cthread->signal.use_sigsuspend_mask == true) {
    cthread->signal.sig_mask = cthread->signal.sigsuspend_oldmask;
    cthread->signal.use_sigsuspend_mask = false;
  }
  
  sig = pick_signal(caught_signals);
  
  if (sig == 0) {   // Shouldn't happen
    return;
  }
  
  Info ("pick_signal returned: %d", sig);
    
  cthread->signal.sig_pending &= ~SIGBIT(sig);

  old_mask = cthread->signal.sig_mask;

  if (cproc->signal.sig_nodefer & SIGBIT(sig)) {
    cthread->signal.sig_mask |= ~cproc->signal.handler_mask[sig-1];
  } else {
    cthread->signal.sig_mask |= SIGBIT(sig) | ~cproc->signal.handler_mask[sig-1];
  }

  if (cproc->signal.restorer == NULL) {
    do_signal_default(sig);
    // TODO: if default behaviour is to ignore, then pick another signal
    return;    

  }	else if (cproc->signal.handler[sig-1] == SIG_DFL) {			
    do_signal_default(sig);				
    cthread->signal.sig_mask = old_mask;
    // TODO: if default behaviour is to ignore, then pick another signal
    return;    
 
  }	else if (cproc->signal.handler[sig-1] == SIG_IGN) {
    cthread->signal.sig_mask = old_mask;

    if (cproc->signal.sig_resethand & SIGBIT(sig)) {
	    cproc->signal.handler[sig-1] = SIG_DFL;
    }
   
    // TODO: if we ignore, then pick another signal
    return;  
  }
  
  if (cproc->signal.sig_resethand & SIGBIT(sig)) {
    cproc->signal.handler[sig-1] = SIG_DFL;
  } 

  sigframe.sf_uc.uc_mcontext.sp = uc->sp;
  sigframe.sf_uc.uc_mcontext.lr = uc->lr;
  sigframe.sf_uc.uc_mcontext.cpsr = uc->cpsr;    // Should be svc_mode spsr  (Mask it?)			  
  sigframe.sf_uc.uc_mcontext.r1 = uc->r1;
  sigframe.sf_uc.uc_mcontext.r2 = uc->r2;
  sigframe.sf_uc.uc_mcontext.r3 = uc->r3;
  sigframe.sf_uc.uc_mcontext.r4 = uc->r4;
  sigframe.sf_uc.uc_mcontext.r5 = uc->r5;
  sigframe.sf_uc.uc_mcontext.r6 = uc->r6;
  sigframe.sf_uc.uc_mcontext.r7 = uc->r7;
  sigframe.sf_uc.uc_mcontext.r8 = uc->r8;
  sigframe.sf_uc.uc_mcontext.r9 = uc->r9;
  sigframe.sf_uc.uc_mcontext.r10 = uc->r10;
  sigframe.sf_uc.uc_mcontext.r11 = uc->r11;
  sigframe.sf_uc.uc_mcontext.r12 = uc->r12;
  sigframe.sf_uc.uc_mcontext.r0 = uc->r0;
  sigframe.sf_uc.uc_mcontext.pc = uc->pc;        // Should be svc_mode LR register.
  sigframe.sf_uc.uc_sigmask = old_mask;
    
  u_sigframe = (struct sigframe *) ALIGN_DOWN (uc->sp - sizeof (struct sigframe), 16);
 
  sigframe.sf_signum = sig;
  
  if (cproc->signal.sig_info & SIGBIT(sig)) {
    sigframe.sf_siginfo = &u_sigframe->sf_si;
  } else {
    sigframe.sf_siginfo = NULL;
  }
  
  sigframe.sf_ucontext = &u_sigframe->sf_uc;
  sigframe.sf_ahu.sf_action = (void (*)(int, siginfo_t *, void *)) cproc->signal.handler[sig-1];
							        
  if (CopyOut (u_sigframe, &sigframe, sizeof (struct sigframe)) != 0) {		
    sig_exit(SIGSEGV);
  }
  
  uc->sp = (uint32_t) u_sigframe;
  uc->pc = (uint32_t) cproc->signal.restorer;
}


