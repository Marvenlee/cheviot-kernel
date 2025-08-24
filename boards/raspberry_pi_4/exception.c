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
#include <sys/mman.h>
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
  struct Thread *cthread;
  
	Error("Unknown syscall called");
  
  cthread = get_current_thread();
  cthread->signal.si_code[SIGSYS-1] = 0;
  do_signal_thread(cthread, SIGSYS, 0, 0);

}


/*
 */
void sys_deprecatedsyscall(void)
{
  struct Thread *cthread;

	Error("deprecated syscall called");

  cthread = get_current_thread();
  cthread->signal.si_code[SIGSYS-1] = 0;
  do_signal_thread(cthread, SIGSYS, 0, 0);
}


/*
 */
void undef_instr_handler(struct UserContext *context)
{
  struct Thread *cthread;
  uint32_t mode;
  
  mode = context->cpsr & CPSR_MODE_MASK;
  
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();
  }

  Info("undef_instr_handler");
    
  cthread = get_current_thread();

  if (mode != USR_MODE && mode != SYS_MODE) {
    KernelPanic();
  }

  Error("pc addr:%08x", (uint32_t)context->pc);

  do_signal_thread(cthread, SIGILL, 0, (intptr_t)context->pc);
  KernelPanic();    // FIXME: Remove

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
  struct Thread *cthread;
  
  cthread = get_current_thread();

  fault_addr = context->pc;

  mode = context->cpsr & CPSR_MODE_MASK;
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();  
  } else if (bkl_owner != cthread) {
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
      
      do_signal_thread(cthread, SIGSYS, 0, (intptr_t)fault_addr);
      
      KernelPanic();    // FIXME: Remove
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
  struct Thread *cthread;
  struct Process *cproc;
  

  cthread = get_current_thread();
  cproc = get_current_process();
  
  dfsr = hal_get_dfsr();
  fault_addr = hal_get_far();

  mode = context->cpsr & CPSR_MODE_MASK;
  if (mode == USR_MODE || mode == SYS_MODE) {
    KernelLock();
  } else if (bkl_owner != cthread) {
    DisableInterrupts();
    PrintUserContext(context);
    Error("fault addr = %08x", fault_addr);
    Error("dfsr = %08x", dfsr);
    KernelPanic();
  }

  status = DFSR_STATUS(dfsr);

  if (status == DFSR_ALIGNMENT_FAULT) {
	  if (mode == USR_MODE) {
        do_signal_thread(cthread, SIGSEGV, 0, (intptr_t)fault_addr);

  	    Error("Alignment fault in user_space panic");
  	    Error("align fault addr: %08x", fault_addr);      
        KernelPanic();    // FIXME: Remove
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
      if (mode == SVC_MODE && cthread->catch_state.pc != 0xfee15bad) {
        Error("Page fault failed during copyin/copyout");
        Error("fault_addr: %08x, access:%08x", fault_addr, access);
        
        context->pc = cthread->catch_state.pc;
        cthread->catch_state.pc = 0xfee15bad;
      } else if (mode == USR_MODE || mode == SYS_MODE) {
        PrintUserContext(context);
        Error("Unhandled USER Data Abort: fault addr = %08x", fault_addr);
        Error("Stack:");
        PrintMemDump(context->sp, 32);
        Error("PC:");
        PrintMemDump(context->pc, 32);
        Error("mode = %08x", mode);
        
        do_signal_thread(cthread, SIGSEGV, 0, (intptr_t)fault_addr);

        KernelPanic();  // FIXME: Remove

      } else {
        Error("Unhandled fault, mode = %08x", mode);
        PrintUserContext(context);
        Error("fault addr = %08x", fault_addr);
        pmap_switch(cproc, NULL);
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

