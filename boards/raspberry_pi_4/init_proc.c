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
 * Kernel initialization.
 */

//#define KDEBUG  1

#include <kernel/arch.h>
#include <kernel/board/boot.h>
#include <kernel/board/globals.h>
#include <kernel/board/init.h>
#include <kernel/board/task.h>
#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/timer.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <sys/time.h>


/* @brief Initialize process management data structures
 *
 * Initializes kernel tables relating to processes, ipc, scheduling and timers.
 * Starts the first process, root_process by calling SwitchToRoot().
 * This loads the FPU state, switches the stack pointer into the kernel
 * and reloads the page directory.  When this is done no lower memory which
 * includes this initialization code will be visible.
 */

void init_processes(void)
{
  struct Process *proc;
  struct Thread *thread;
  
  Info("InitProcesses.,");
  
  bkl_locked = false;
  bkl_owner = NULL;
  LIST_INIT(&bkl_blocked_list);

  LIST_INIT(&thread_reaper_detached_thread_list);
  InitRendez(&thread_reaper_rendez);
  
  for (int t = 0; t < NIRQ; t++) {
    LIST_INIT(&isr_handler_list[t]);
    irq_handler_cnt[t]=0;
  }
  
  for (int t = 0; t < 32; t++) {
    CIRCLEQ_INIT(&sched_queue[t]);
  }

  Info("isr_handler and sched queue initialized");

  memset (pid_table, 0, max_pid * sizeof (struct PidDesc));

  LIST_INIT(&free_piddesc_list);
  for (int t = 0; t < max_pid; t++) {
    LIST_ADD_TAIL(&free_piddesc_list, &pid_table[t], free_link);
  }

  Info("free piddesc list initialized");
  
  LIST_INIT(&free_session_list);  
  for (int t = 0; t < max_pid; t++) {
    LIST_ADD_TAIL(&free_session_list, &session_table[t], free_link);
  }

  Info("free session list initialized");

  LIST_INIT(&free_pgrp_list);  
  for (int t = 0; t < max_pid; t++) {
    LIST_ADD_TAIL(&free_pgrp_list, &pgrp_table[t], free_link);
  }

  Info("free pgrp list initialized");
  
  free_process_cnt = max_process;
  LIST_INIT(&free_process_list);  
  
  for (int t = 0; t < max_process; t++) {
    proc = &process_table[t];
    memset(proc, 0xf001f001, sizeof *proc);

    proc->state = PROC_STATE_FREE;
    LIST_ADD_TAIL(&free_process_list, proc, free_link);
  }

  Info("free process list initialized");

  // free_thread_cnt = max_thread;
  LIST_INIT(&free_thread_list);

  for (int t = 0; t < max_thread; t++) {
    thread = &thread_table[t];
    thread->state = THREAD_STATE_FREE;
    LIST_ADD_TAIL(&free_thread_list, thread, free_link);
  }
   
  Info("free thread list initialized");
  
  LIST_INIT(&free_futex_list);
  
  for (int t = 0; t < max_futex; t++) {
    LIST_ADD_TAIL(&free_futex_list, &futex_table[t], link);
  }
  
  futex_table_busy = 0;
  InitRendez(&futex_table_busy_rendez);
  
  for (int t = 0; t < FUTEX_HASH_SZ; t++) {
    LIST_INIT(&futex_hash_table[t]);
  }
  
  Info("futex lists initialized");
    
  for (int t = 0; t < JIFFIES_PER_SECOND; t++) {
    LIST_INIT(&timing_wheel[t]);
  }
  
  Info(".. timing wheel inited");
  
  InitRendez(&timer_rendez);
  softclock_time = hardclock_time = 0;
  
  init_cpu_tables();
  
  Info(".. cpu struct inited");

  root_process = do_create_process(exec_root, NULL,
                              SCHED_RR, 16,
                              PROCF_ALLOW_IO, 
                              "root",
                              &cpu_table[0]);

  Info("root process created");

  // Does the timer thread and kernel threads run in the root address space?
  // May as well, and then use the ASID mechanism to reduce overhead.

  thread_reaper_thread = do_create_thread(root_process, thread_reaper_task, NULL, NULL, 
                               SCHED_RR, 16, 
                               THREADF_KERNEL, false, 
                               NULL, 0,
                               NULL,
                               0,
                               &cpu_table[0],
                               "reaper-kt");
                               
  Info("thread reaper thread created, tid:%d", get_thread_tid(thread_reaper_thread));

  thread_start(thread_reaper_thread);

  
  // Does the timer thread and kernel threads run in the root address space?
  // May as well, and then use the ASID mechanism to reduce overhead.

  timer_thread = do_create_thread(root_process, timer_bottom_half_task, NULL, NULL,
                               SCHED_RR, 31, 
                               THREADF_KERNEL, false, 
                               NULL, 0,
                               NULL,
                               0,
                               &cpu_table[0],
                               "timer-kt");
                               
  Info("timer thread created, tid:%d", get_thread_tid(timer_thread));

  thread_start(timer_thread);

  cpu_table[0].idle_thread = do_create_thread(root_process, idle_task, NULL, NULL,
                                   SCHED_IDLE, 0, 
                                   THREADF_KERNEL, false, 
                                   NULL, 0,
                                   NULL,
                                   0,
                                   &cpu_table[0],
                                   "idle-kt");

  cpu_table[0].idle_thread->state = THREAD_STATE_READY;

  Info("idle thread created for cpu 0, tid:%d", get_thread_tid(cpu_table[0].idle_thread));
}


/*
 * FIXME: How do we start scheduler on multiple CPUs?
 * How to release other CPUs from reset.
 */
void start_scheduler(void)
{
  struct CPU *cpu = &cpu_table[0];
  struct Thread *next;
  int q;
  
  Info("start_scheduler()");
        
  for (q = 31; q >= 0; q--) {
    if ((sched_queue_bitmap & (1 << q)) != 0) {
      break;
    }
  }
  
  next = CIRCLEQ_HEAD(&sched_queue[q]);

  if (next == NULL) {
    next = cpu->idle_thread;
  }

  KASSERT(next != NULL);
  
  next->state = THREAD_STATE_RUNNING;    

  pmap_switch(next->process, NULL);

  cpu->current_thread = next;
  cpu->current_process = next->process;

  NotifyLoggerProcessesInitialized();
  
  GetContext(next->context);
}


/*
 *
 */
void init_cpu_tables(void)
{
  struct CPU *cpu;
  max_cpu = 1;
  cpu_cnt = max_cpu;
  cpu = &cpu_table[0];
  cpu->svc_stack = (vm_addr)&svc_stack_top;
  cpu->interrupt_stack = (vm_addr)&interrupt_stack_top;
  cpu->exception_stack = (vm_addr)&exception_stack_top;
  cpu->current_process = NULL;
  cpu->current_thread = NULL;
}

