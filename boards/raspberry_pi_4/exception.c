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

#define KDEBUG

#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/signal.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <sys/signal.h>
#include <ucontext.h>


/*
 *
 */
void reset_handler(void)
{
  KernelPanic();
}


/*
 *
 */
void reserved_handler(void)
{
  Info("reserved_handler");
  KernelPanic();
}


/*
 *
 */
void fiq_handler(void)
{
  Info("fiq_handler");
  KernelPanic();
}


/*
 */
void sys_unknownsyscallhandler(void)
{
  struct Process *current_proc;
  
	Error("Unknown syscall called");
  
  current_proc = get_current_process();
  current_proc->signal.si_code[SIGSYS-1] = 0;
  sys_kill(current_proc->pid, SIGSYS);
}


/*
 */
void sys_deprecatedsyscall(void)
{
  struct Process *current_proc;

	Error("deprecated syscall called");

  current_proc = get_current_process();
  current_proc->signal.si_code[SIGSYS-1] = 0;
  sys_kill(current_proc->pid, SIGSYS);
}


/*
 */
void undef_instr_handler(struct UserContext *context)
{
  struct Process *current_proc;
  uint32_t mode;
  
  mode = context->cpsr & CPSR_MODE_MASK;
  
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();
  }

  Info("undef_instr_handler");
    
  current_proc = get_current_process();

  if (mode != USR_MODE && mode != SYS_MODE) {
    KernelPanic();
  }

  // FIXME: signal state needs per thread state.
//  current_proc->signal.si_code[SIGILL-1] = 0;
//  current_proc->signal.sigill_ptr = context->pc;
//  sys_kill(current_proc->pid, SIGILL);
    Error("pc addr:%08x", (uint32_t)context->pc);
    KernelPanic();


  check_signals(context);

  if (bkl_locked == false) {
    DisableInterrupts();
    PrintUserContext(context);
    Error("BKL is not locked when returning from undef_instr_handler");
    KernelPanic();
  }

  KernelUnlock();
}


/*
 * FIXME: Do we need to check IFSR?? or determine if this is an alignment issue?
 */
void prefetch_abort_handler(struct UserContext *context)
{
  vm_addr fault_addr;
  uint32_t mode;
  struct Process *current_proc;
  struct Thread *current_thread;
  
  current_proc = get_current_process();
  current_thread = get_current_thread();

  fault_addr = context->pc;

  mode = context->cpsr & CPSR_MODE_MASK;
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();  
  } else if (bkl_owner != current_thread) {
    DisableInterrupts();
    PrintUserContext(context);
    Error("Prefetch Abort bkl not owner, fault addr = %08x", fault_addr);
    KernelPanic();
  } else {
    DisableInterrupts();
    PrintUserContext(context);
    Error("Prefetch Abort in kernel, fault addr = %08x", fault_addr);
    KernelPanic();
  }

  if (page_fault(fault_addr, PROT_EXEC) != 0) {
    if (mode == USR_MODE || mode == SYS_MODE) {
      PrintUserContext(context);
      Error("Prefetch Abort: fault addr = %08x", fault_addr);
      
      // TODO: Signal state needs to be per thread ?
      // current_thread->signal.si_code[SIGSEGV-1] = 0;    // TODO: Could be access bit?
      // current_thread->signal.sigsegv_ptr = fault_addr;
      // do_thread_kill(current_thread, SIGSEGV);
      //sys_kill(current_proc->pid, SIGSEGV): 
      KernelPanic();
    } else {
      PrintUserContext(context);
      Error("In unexpected processor mode, fault addr = %08x", fault_addr);
      KernelPanic();
    }
  }

  KASSERT(context->pc != 0);

  if (mode == USR_MODE || mode == SYS_MODE) {
    check_signals(context);

    if (bkl_locked == false) {
      DisableInterrupts();
      PrintUserContext(context);
      Error("BKL is not locked when returning from prefetch page fault");
      KernelPanic();
    }
    KernelUnlock();
  }
}

/*
 * Page fault when accessing data.
 * When the previous mode is USR or SYS acquire the kernel mutex
 * Only other time is in SVC mode with kernel lock already acquired by
 * this process AND the try/catch pc/sp is initialized.
 * Other modes and times it is a kernel panic.
 */

void data_abort_handler(struct UserContext *context)
{
  volatile bits32_t dfsr;
  bits32_t access;
  volatile vm_addr fault_addr;
  uint32_t mode;
  uint32_t status;  
  struct Process *current_proc;
  struct Thread *current_thread;

  current_proc = get_current_process();  
  current_thread = get_current_thread();

  dfsr = hal_get_dfsr();
  fault_addr = hal_get_far();

  mode = context->cpsr & CPSR_MODE_MASK;
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();
  } else if (bkl_owner != current_thread) {
    DisableInterrupts();
    PrintUserContext(context);
    Error("fault addr = %08x", fault_addr);
    Error("dfsr = %08x", dfsr);
    KernelPanic();
  }

  status = DFSR_STATUS(dfsr);

  if (status == DFSR_ALIGNMENT_FAULT) {
	  if (mode == USR_MODE) {
	      // FIXME: Needs to have signal state per thread
        //current_proc->signal.si_code[SIGSEGV-1] = 0;    // TODO: Could be access bit?
        //current_proc->signal.sigsegv_ptr = fault_addr;
        //sys_kill(current_proc->pid, SIGSEGV);	    
  	    Error("Alignment fault in user_space panic");
  	    Error("align fault addr: %08x", fault_addr);      
        KernelPanic();
	  } else {
	    Error("Alignment fault in kernel, panic");
      KernelPanic();
    }
	} else {
    if (dfsr & DFSR_RW)
      access = PROT_WRITE;
    else
      access = PROT_READ;

    if (page_fault(fault_addr, access) != 0) {
      if (mode == SVC_MODE && current_thread->catch_state.pc != 0xfee15bad) {
        Error("Page fault failed during copyin/copyout");
        Error("fault_addr: %08x, access:%08x", fault_addr, access);
        
        context->pc = current_thread->catch_state.pc;
        current_thread->catch_state.pc = 0xfee15bad;
      } else if (mode == USR_MODE || mode == SYS_MODE) {
        PrintUserContext(context);
        Error("Unhandled USER Data Abort: fault addr = %08x", fault_addr);
        Error("Stack:");
        PrintMemDump(context->sp, 32);
        Error("PC:");
        PrintMemDump(context->pc, 32);
        Error("mode = %08x", mode);
        

	      // FIXME: Needs to have signal state per thread
        //current_proc->signal.si_code[SIGSEGV-1] = 0;    // TODO: Could be access bit?
        //current_proc->signal.sigsegv_ptr = fault_addr;
        //sys_kill(current_proc->pid, SIGSEGV);

        KernelPanic();  // FIXME: Remove

      } else {
        PrintUserContext(context);
        Error("fault addr = %08x", fault_addr);
        KernelPanic();
      }
    }
  }
  
  KASSERT(context->pc != 0);

  if (mode == USR_MODE || mode == SYS_MODE) {
    check_signals(context);

    if (bkl_locked == false) {
      DisableInterrupts();
      PrintUserContext(context);
      KernelPanic();
    }

    KernelUnlock();
  }
}

