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
 * Scheduling related functions
 */

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/msg.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/arch.h>


/* @brief   Perform a task switch
 *
 * Currently implements round-robin scheduling and a niceness scheduler.
 * Priorities 16 to 31 are for the round-robin scheduler. Priorities
 * 0 to 15 are for the niceness scheduler.
 *
 * The switching of the task's register context for another task's context
 * is done by the following magic:
 *
 * if (SetContext(&context[0]) == 0) {
 *   GetContext(next->context);
 * }
 * 
 * When SetContext is called, the current thread's registers are saved in
 * the context array on its stack. SetContext returns 0 to indicate it should
 * now get the context of the next stack.
 *
 * When SetContext stores the context of the current task it does something
 * perculiar, as mentioned it returns 0 but in the register state that it
 * saves it sets it so that a value of 1 would be returned, On ARM CPUs this
 * will be in register 0.  
 *
 * When GetContext is called the next thread's context is reloaded from its
 * stack into the CPU's registers. These registers are at the point they were saved
 * when it called SetContext. When the registers are reloaded they are what
 * was saved by SetContext so when it performs a return it is returning
 * from SetContext and not GetContext.
 * 
 * However, instead of returning 0 and performing the GetContext again
 * it returns the 1 stored by its original call to SetContext to prevent it
 * endlessly calling GetContext.
 *
 * At this point execution resumes after the "if" statement and the task is
 * switched.
 *
 * FIXME: Can we defer Fair-Share scheduling (e.g. lottery/stride) onto a DPC
 * scheduler task, like the timer task. Effectively this only schedules RTOS
 * round-robin tasks.  If no RTOS tasks, run the DPC task to choose a task.
 */
void Reschedule(void)
{
  context_word_t context[N_CONTEXT_WORD];
  struct Process *current, *next, *proc;
  struct CPU *cpu;
  int q;

  cpu = get_cpu();
  current = get_current_process();

  if (current != NULL) {
    current->quanta_used ++;
  
    if (current->sched_policy == SCHED_RR) {
      KASSERT(current->priority >= 16 && current->priority < 32);

      if ((CIRCLEQ_HEAD(&sched_queue[current->priority])) != NULL) {
        CIRCLEQ_FORWARD(&sched_queue[current->priority], sched_entry);
        current->quanta_used = 0;
      }
    } else if (current->sched_policy == SCHED_OTHER) {
      if (current->quanta_used == SCHED_QUANTA_JIFFIES) {      
        if (current->priority > 1) {
          CIRCLEQ_REM_HEAD(&sched_queue[current->priority], sched_entry);              
          current->priority--;
          CIRCLEQ_ADD_TAIL(&sched_queue[current->priority], current, sched_entry);        
        } else {
          CIRCLEQ_FORWARD(&sched_queue[current->priority], sched_entry);          
        }       
        current->quanta_used = 0;
      }     
    }
  }

  next = NULL;

  if (sched_queue_bitmap != 0) {
    for (q = 31; q >= 0; q--) {
      if ((sched_queue_bitmap & (1 << q)) != 0)
        break;
    }

    next = CIRCLEQ_HEAD(&sched_queue[q]);
  }

  if (next == NULL) {
    next = cpu->idle_process;
  }

  if (next != NULL) {
    next->state = PROC_STATE_RUNNING;
    pmap_switch(next, current);
  }

  if (next != current) {
    current->context = &context;
    cpu->current_process = next;

    if (SetContext(&context[0]) == 0) {
      GetContext(next->context);
    }
  }
}

/* @brief   Add process to a ready queue based on its scheduling policy and priority.
 */
void SchedReady(struct Process *proc)
{
  struct Process *next;
  struct CPU *cpu;

  cpu = get_cpu();

  if (proc->sched_policy == SCHED_RR || proc->sched_policy == SCHED_FIFO) {
    CIRCLEQ_ADD_TAIL(&sched_queue[proc->priority], proc, sched_entry);
    sched_queue_bitmap |= (1 << proc->priority);

  } else if (proc->sched_policy == SCHED_OTHER) {
    CIRCLEQ_ADD_TAIL(&sched_queue[proc->priority], proc, sched_entry);
    sched_queue_bitmap |= (1 << proc->priority);

  } else {
    Error("Ready: Unknown sched policy %d", proc->sched_policy);
    KernelPanic();
  }

  proc->quanta_used = 0;
  cpu->reschedule_request = true;
}


/* @brief   Removes process from the ready queue.
 */
void SchedUnready(struct Process *proc)
{
  struct CPU *cpu;

  cpu = get_cpu();

  if (proc->sched_policy == SCHED_RR || proc->sched_policy == SCHED_FIFO) {
    CIRCLEQ_REM_ENTRY(&sched_queue[proc->priority], proc, sched_entry);
    if (CIRCLEQ_HEAD(&sched_queue[proc->priority]) == NULL) {
      sched_queue_bitmap &= ~(1 << proc->priority);
    }

    proc->quanta_used = 0;
    
  } else if (proc->sched_policy == SCHED_OTHER) {
    CIRCLEQ_REM_ENTRY(&sched_queue[proc->priority], proc, sched_entry);
    if (CIRCLEQ_HEAD(&sched_queue[proc->priority]) == NULL) {
      sched_queue_bitmap &= ~(1 << proc->priority);
    }

    proc->priority = proc->desired_priority;
    proc->quanta_used = 0;

  } else {
    Error("Unready: Unknown sched policy *****");
    KernelPanic();
  }

  cpu->reschedule_request = true;
}


/* @brief   Set process scheduling policy and priority
 */
int sys_setschedparams(int policy, int priority)
{
  struct Process *current;
  current = get_current_process();

  if (policy == SCHED_RR || policy == SCHED_FIFO) {
    if (!io_allowed(current)) {
      return -EPERM;
    }

    if (priority < 16 || priority > 31) {
      return -EINVAL;
    }

    DisableInterrupts();
    SchedUnready(current);
    current->sched_policy = policy;
    current->desired_priority = priority;
    current->priority = priority;
    SchedReady(current);
    Reschedule();
    EnableInterrupts();

    return 0;

  } else if (policy == SCHED_OTHER) {
    if (priority < 0 || priority > 15) {
      return -EINVAL;
    }

    DisableInterrupts();
    SchedUnready(current);
    current->sched_policy = policy;
    current->desired_priority = priority;
    current->priority = priority;
    SchedReady(current);
    Reschedule();
    EnableInterrupts();

    return 0;
    
  } else {
    return -EINVAL;
  }
}


/*
 *
 */
int sys_getpriority(void)
{
	return 0;
}


/*
 *
 */
int sys_setpriority(void)
{
	return 0;
}


