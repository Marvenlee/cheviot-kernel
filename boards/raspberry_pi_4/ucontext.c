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
 * ARM-specific thread user/kernel context handling.
 */

//#define KDEBUG
 
#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/board/init.h>
#include <kernel/board/task.h>
#include <kernel/dbg.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <string.h>


/*
 * TODO: Remove me, added for debugging
 */
void start_kernel_thread_log(void)
{
  Info ("start_kernel_thread_log()");
}

void start_forked_thread_log(void)
{
  Info ("start_forked_thread_log()");
}

void start_user_thread_log(void)
{
  Info ("start_user_thread_log()");
}

void start_prolog_user_thread_log(void)
{
  Info ("start_prolog_user_thread_log()");
}


void start_forked_thread_inside_log(struct UserContext *uc)
{
  Info ("start_forked_thread_inside_log()");

  Info ("uc->r0 = %08x", uc->r0);
  Info ("uc->sp = %08x", uc->sp);
  Info ("uc->lr = %08x", uc->lr);
  Info ("uc->pc = %08x", uc->pc);
}


/*
 * TODO: Remove arch_pick_cpu from below arch_ functions.  cpu is supplied into
 * do_create_thread
 */
struct CPU *arch_pick_cpu(void)
{
  return &cpu_table[0];
}


/* @brief   Set CPU registers in preparation for a process forking
 *
 * Executed on the context of the parent process. Initializes CPU state
 * and kernel stack so that PrepareProcess() is executed on the context
 * of the new process.
 */
int arch_init_fork_thread(struct Process *new_proc, struct Process *current_proc, 
                      struct Thread *new_thread, struct Thread *current_thread)
{
  struct UserContext *uc_current;
  struct UserContext *uc_new;
  uint32_t *context;

  Info("arch_forked_thread(new_thread:%08x)", new_thread);

  uc_current = (struct UserContext *)((vm_addr)current_thread->stack + KERNEL_STACK_SZ -
                                      sizeof(struct UserContext));
                                      
  uc_new = (struct UserContext *)((vm_addr)new_thread->stack + KERNEL_STACK_SZ -
                                   sizeof(struct UserContext));

  uc_new->r0 = 0;       // We return 0 to indicate we're new process (parent gets PID)
  uc_new->r1 = uc_current->r1;
  uc_new->r2 = uc_current->r2;
  uc_new->r3 = uc_current->r3;
  uc_new->r4 = uc_current->r4;
  uc_new->r5 = uc_current->r5;
  uc_new->r6 = uc_current->r6;
  uc_new->r7 = uc_current->r7;
  uc_new->r8 = uc_current->r8;
  uc_new->r9 = uc_current->r9;
  uc_new->r10 = uc_current->r10;
  uc_new->r11 = uc_current->r11;
  uc_new->r12 = uc_current->r12;
  uc_new->sp = uc_current->sp;
  uc_new->lr = uc_current->lr;
  uc_new->pc = uc_current->pc;
  uc_new->cpsr = uc_current->cpsr;

  Info ("arch_fork_process sp = %08x", uc_new->sp);
  Info ("arch_fork_process lr = %08x", uc_new->lr);
  Info ("arch_fork_process pc = %08x", uc_new->pc);
  Info ("arch_fork_process cpsr = %08x", uc_new->cpsr);

  context = ((uint32_t *)uc_new) - N_CONTEXT_WORD;

  KASSERT(uc_new->pc != 0);

  for (int t = 0; t < 13; t++) {
    context[t] = 0xf005ba10 + t;
  }

  context[13] = (uint32_t)uc_new;
  context[14] = (uint32_t)start_of_forked_process;

  save_fp_context(&context[15]);  // Save 16+1 of current thread onto new thread's context

  new_thread->context = context;

  Info ("arch_fork_process context = %08x", (uint32_t)new_thread->context);

  new_thread->catch_state.pc = 0xfee15bad;
  
  return 0;
}


/* @brief   Set CPU registers in preparation for a process Exec'ing
 *
 */
void arch_init_exec_thread(struct Process *proc, struct Thread *thread, void *entry_point,
                  void *stack_pointer, struct execargs *args)
{
  struct UserContext *uc;
  uint32_t *context;
  uint32_t cpsr;

  Info("arch_init_exec_thread");
  Info("sp:%08x", (uint32_t)stack_pointer);
  Info("pc:%08x", (uint32_t)entry_point);
  Info("args:%08x", (uint32_t)args);

  cpsr = USR_MODE | CPSR_DEFAULT_BITS; 

  uc = (struct UserContext *)((vm_addr)thread->stack + KERNEL_STACK_SZ -
                              sizeof(struct UserContext));

  Info("k uc:%08x", (uint32_t)uc);

  memset(uc, 0, sizeof(*uc));

  uc->r0 = args->argc;
  uc->r1 = (uint32_t)args->argv;
  uc->r2 = args->envc;
  uc->r3 = (uint32_t)args->envv;
  uc->pc = (uint32_t)entry_point;
  uc->sp = (uint32_t)stack_pointer;
  uc->cpsr = cpsr;

  Info ("arch_init_exec_thread");
  Info ("cpsr = %08x", uc->cpsr);
  Info ("sp = %08x", uc->sp);
  Info ("pc = %08x", uc->pc);

  context = ((uint32_t *)uc) - N_CONTEXT_WORD;

  for (int t = 0; t < 13; t++) {
    context[t] = 0;
  }

  context[13] = (uint32_t)uc;
  context[14] = (uint32_t)start_of_execed_process;

  context[15] = 0x00000000;  // FPU status register
  for (int t=0; t<32; t+=2) {
    context[16 + t ] = 0x00000000;
    context[16 + t + 1] = 0x7FF00000;  // SNaN
  }

  thread->context = context;
  thread->catch_state.pc = 0xfee15bad;
  
  GetContext(thread->context);
}





/*
 *
 */
void arch_init_user_thread(struct Thread *thread, void *entry, void *user_entry, void *stack_pointer, void *arg)
{
  uint32_t cpsr;
  struct UserContext *uc;
  uint32_t *context;

  uc = (struct UserContext *)((vm_addr)thread->stack + KERNEL_STACK_SZ -
                              sizeof(struct UserContext));

#if 1
  cpsr = USR_MODE | CPSR_DEFAULT_BITS;
#else
  cpsr = cpsr_dnm_state | USR_MODE | CPSR_DEFAULT_BITS;
#endif
  
  memset(uc, 0, sizeof(*uc));
  uc->cpsr = cpsr;

  if (entry == NULL) {
    uc->pc = (uint32_t)user_entry;      // Why are these set to odd addresses?
    uc->sp = (uint32_t)stack_pointer;
    uc->r0 = (uint32_t)arg;
  } else {
    uc->pc = (uint32_t)0xdeadeee3;      // Why are these set to odd addresses?
    uc->sp = (uint32_t)0xdeadaaa3;
  }
  
  // kernel save/restore context

  context = ((uint32_t *)uc) - N_CONTEXT_WORD;
  
  context[0] = (uint32_t)arg;  
  context[1] = (uint32_t)entry;
  
  for (int t = 2; t <= 12; t++) {
    context[t] = 0;
  }

  Info("Setting FPU state, addr:%08x", (uint32_t)context); 

  context[13] = (uint32_t)uc;
  
  if (entry != NULL) {
    context[14] = (uint32_t)start_of_prolog_user_thread;
  } else {
    context[14] = (uint32_t)start_of_user_thread;
  }
  
  
  context[15] = 0x00000000;  // FPU status register
  for (int t=0; t<32; t+=2) {
    context[16 + t ] = 0x00000000;
    context[16 + t + 1] = 0x7FF00000;  // SNaN
  }
  
  Info("Set FPU state, addr:%08x", (uint32_t)context); 

  thread->context = context;
  thread->cpu = arch_pick_cpu();  
  thread->catch_state.pc = 0xfee15bad;
}


/*
 *
 */
void arch_init_kernel_thread(struct Thread *thread, void *entry, void *arg)
{
  struct UserContext *uc;
  uint32_t *context;
  uint32_t cpsr;
  
#if 1
  cpsr = SYS_MODE | CPSR_DEFAULT_BITS;
#else
  cpsr = cpsr_dnm_state | SYS_MODE | CPSR_DEFAULT_BITS;
#endif
  
  uc = (struct UserContext *)((vm_addr)thread->stack + KERNEL_STACK_SZ
                              - sizeof(struct UserContext));
  memset(uc, 0, sizeof(*uc));
  uc->pc = (uint32_t)0xdeadeee3;
  uc->cpsr = cpsr;
  uc->sp = (uint32_t)0xdeadeee1;

  // kernel save/restore context

  context = ((uint32_t *)uc) - N_CONTEXT_WORD;

  context[0] = (uint32_t)arg;
  context[1] = (uint32_t)entry;
  
  for (int t = 2; t <= 12; t++) {
    context[t] = 0;
  }

  context[13] = (uint32_t)uc;
  context[14] = (uint32_t)start_of_kernel_thread;

  context[15] = 0x00000000;  // FPU status register
  for (int t=0; t<32; t+=2) {
    context[16 + t ] = 0x00000000;
    context[16 + t + 1] = 0x7FF00000;  // SNaN
  }

  thread->context = context;  
  thread->cpu = arch_pick_cpu();
  thread->catch_state.pc = 0xfee15bad;
}


/* @brief   Architecture specific handling when freeing a thread
 */
void arch_stop_thread(struct Thread *thread)
{
}


