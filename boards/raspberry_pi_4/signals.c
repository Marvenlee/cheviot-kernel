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
 * KernelExit function.  Called prior to returning to user-mode by
 * Software Interrupts (SWI), interrupts and exceptions.  Also the
 * path that is taken whenever a process is put to sleep in the middle
 * of a system call.
 */

#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/board/task.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/proc.h>
#include <kernel/signal.h>
#include <kernel/types.h>
#include <ucontext.h>


/*
 * SigReturn();
 */
void sys_sigreturn(struct sigframe *u_sigframe)
{
	struct sigframe sigframe;
	struct Process *current;
	struct UserContext *uc;
	
	current = get_current_process();
	
	uc = (struct UserContext *)((vm_addr)current + PROCESS_SZ - sizeof(struct UserContext));

	if (CopyIn (&sigframe, u_sigframe, sizeof (struct sigframe)) != 0) {	
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
						
	current->signal.sig_mask = sigframe.sf_uc.uc_sigmask;
}

/*
 * DeliverUserSignals();
 *
 * Called just before returning from a syscall, interrupt or exception by the
 * wrappers in the stubs.s file.  Only called if we are returning to PL3 and not
 * to virtual-86 mode.
 *
 * Interrupts are disabled for the duration of this function, though it should
 * only need slight modification to enable interrupts for most of it.
 *
 * sig_exit() is responsible for enabling interrupts if a signal cannot be
 * delivered or it's default action is to kill the process.
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
	sigset_t safe_signals;
	sigset_t sync_signals;
	sigset_t old_mask;
	int sig;
	struct Process *current;

	current = get_current_process();

  if (current->signal.sig_pending == 0) {
    return;
  }
  	
	sync_signals = current->signal.sig_pending & SYNCSIGMASK;

	if (sync_signals != 0 && (sync_signals & current->signal.sig_mask) != 0) {
		sig = pick_signal(sync_signals & ~current->signal.sig_mask);
		sig_exit(sig);
	}
		
	safe_signals = current->signal.sig_pending & ~current->signal.sig_mask;
		
	if (safe_signals)	{	
		if (current->signal.use_sigsuspend_mask == true) {
			current->signal.sig_mask = current->signal.sigsuspend_oldmask;
			current->signal.use_sigsuspend_mask = false;
		}
			
		if (safe_signals & SIGBIT(SIGKILL)) {
			sig_exit(SIGKILL);	
		
		}	else if (safe_signals != SIGBIT(SIGCONT)) {
			sig = pick_signal(safe_signals);
				
			current->signal.sig_pending &= ~SIGBIT(sig);
			
			old_mask = current->signal.sig_mask;
			
			if (current->signal.sig_nodefer & SIGBIT(sig)) {
				current->signal.sig_mask |= ~current->signal.handler_mask[sig-1];
			} else {
				current->signal.sig_mask |= SIGBIT(sig) | ~current->signal.handler_mask[sig-1];
			}
			
			if (current->signal.restorer == NULL) {
				do_signal_default(sig);
				
			}	else if (current->signal.handler[sig-1] == SIG_DFL) {			
				do_signal_default(sig);				
				current->signal.sig_mask = old_mask;
				
			}	else if (current->signal.handler[sig-1] == SIG_IGN) {
				current->signal.sig_mask = old_mask;
				if (current->signal.sig_resethand & SIGBIT(sig)) {
					current->signal.handler[sig-1] = SIG_DFL;
				}
				
			} else {
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
				
				if (current->signal.sig_info & SIGBIT(sig)) {
					sigframe.sf_siginfo = &u_sigframe->sf_si;
				} else {
					sigframe.sf_siginfo = NULL;
				}
				
				sigframe.sf_ucontext = &u_sigframe->sf_uc;
				sigframe.sf_ahu.sf_action = (void (*)(int, siginfo_t *, void *)) current->signal.handler[sig-1];
												
				if (current->signal.sig_resethand & SIGBIT(sig)) {
					current->signal.handler[sig-1] = SIG_DFL;
        }
        
				if (CopyOut (u_sigframe, &sigframe, sizeof (struct sigframe)) == 0) {		
					uc->sp = (uint32_t) u_sigframe;
					uc->pc = (uint32_t) current->signal.restorer;
				}	else {
					sig_exit(SIGSEGV);
				}
			}
		}
	}
}


/*
 */
int pick_signal(uint32_t sigbits)
{
	int sig;

  if (sigbits == 0) {
    return 0;
  }
  
  sig = 0;
  
  if ((sigbits & 0xFFFF0000) == 0) {
     sig += 16; 
     sigbits <<= 16;
  }
  
  if ((sigbits & 0xFF000000) == 0) {
    sig += 8;
    sigbits <<= 8;
  }
  
  if ((sigbits & 0xF0000000) == 0) {
    sig += 4; 
    sigbits <<= 4;
  }
  
  if ((sigbits & 0xC0000000) == 0) { 
    sig += 2;
    sigbits <<= 2;
  }
  
  if ((sigbits & 0x80000000) == 0) {
    sig += 1;
  }  
    
  return sig + 1;
}

