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
  kernelpanic();
}


/*
 *
 */
void reserved_handler(void)
{
  klog_info("reserved_handler");
  kernelpanic();
}


/*
 *
 */
void fiq_handler(void)
{
  klog_info("fiq_handler");
  kernelpanic();
}


/*
 */
void sys_unknownsyscallhandler(void)
{
  struct Thread *cthread;
  
	klog_error("Unknown syscall called");
  
  cthread = get_current_thread();
  cthread->signal.si_code[SIGSYS-1] = 0;
  do_signal_thread(cthread, SIGSYS, 0, 0);

}


/*
 */
void sys_deprecatedsyscall(void)
{
  struct Thread *cthread;

	klog_error("deprecated syscall called");

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

  klog_info("undef_instr_handler");
    
  cthread = get_current_thread();

  if (mode != USR_MODE && mode != SYS_MODE) {
    kernelpanic();
  }

  klog_error("pc addr:%08x", (uint32_t)context->pc);

  do_signal_thread(cthread, SIGILL, 0, (intptr_t)context->pc);
  kernelpanic();    // FIXME: Remove

  check_signals(context);

  if (bkl_locked == false) {
    DisableInterrupts();
    PrintUserContext(context);
    klog_error("BKL is not locked when returning from undef_instr_handler");
    kernelpanic();
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
    klog_error("Prefetch Abort bkl not owner, fault addr = %08x", fault_addr);
    kernelpanic();
  } else {
    DisableInterrupts();
    PrintUserContext(context);
    klog_error("Prefetch Abort in kernel, fault addr = %08x", fault_addr);
    kernelpanic();
  }

  if (page_fault(fault_addr, PROT_EXEC) != 0) {
    if (mode == USR_MODE || mode == SYS_MODE) {
      PrintUserContext(context);
      klog_error("Prefetch Abort: fault addr = %08x", fault_addr);
      
      do_signal_thread(cthread, SIGSYS, 0, (intptr_t)fault_addr);
      
      kernelpanic();    // FIXME: Remove
    } else {
      PrintUserContext(context);
      klog_error("In unexpected processor mode, fault addr = %08x", fault_addr);
      kernelpanic();
    }
  }

  kassert(context->pc != 0);

  if (mode == USR_MODE || mode == SYS_MODE) {
    check_signals(context);

    if (bkl_locked == false) {
      DisableInterrupts();
      PrintUserContext(context);
      klog_error("BKL is not locked when returning from prefetch page fault");
      kernelpanic();
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
    klog_error("fault addr = %08x", fault_addr);
    klog_error("dfsr = %08x", dfsr);
    kernelpanic();
  }

  status = DFSR_STATUS(dfsr);

  if (status == DFSR_ALIGNMENT_FAULT) {
	  if (mode == USR_MODE) {
        do_signal_thread(cthread, SIGSEGV, 0, (intptr_t)fault_addr);

  	    klog_error("Alignment fault in user_space panic");
  	    klog_error("align fault addr: %08x", fault_addr);      
        kernelpanic();    // FIXME: Remove
	  } else {
	    klog_error("Alignment fault in kernel, panic");
      kernelpanic();
    }
	} else {
    if (dfsr & DFSR_RW)
      access = PROT_WRITE;
    else
      access = PROT_READ;

    if (page_fault(fault_addr, access) != 0) {
      if (mode == SVC_MODE && cthread->catch_state.pc != 0xfee15bad) {
        klog_error("Page fault failed during copyin/copyout");
        klog_error("fault_addr: %08x, access:%08x", fault_addr, access);
        
        context->pc = cthread->catch_state.pc;
        cthread->catch_state.pc = 0xfee15bad;
      } else if (mode == USR_MODE || mode == SYS_MODE) {
        PrintUserContext(context);
        klog_error("Unhandled USER Data Abort: fault addr = %08x", fault_addr);
        klog_error("Stack:");
        PrintMemDump(context->sp, 32);
        klog_error("PC:");
        PrintMemDump(context->pc, 32);
        klog_error("mode = %08x", mode);
        
        do_signal_thread(cthread, SIGSEGV, 0, (intptr_t)fault_addr);

        kernelpanic();  // FIXME: Remove

      } else {
        klog_error("Unhandled fault, mode = %08x", mode);
        PrintUserContext(context);
        klog_error("fault addr = %08x", fault_addr);
        pmap_switch(cproc, NULL);
        kernelpanic();
      }
    }
  }
  
  kassert(context->pc != 0);

  if (mode == USR_MODE || mode == SYS_MODE) {
    check_signals(context);

    if (bkl_locked == false) {
      DisableInterrupts();
      PrintUserContext(context);
      kernelpanic();
    }

    KernelUnlock();
  }
}

