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

//#define KDEBUG

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
  struct Thread *current, *next, *thread;
  struct Process *current_proc, *next_proc;
  struct CPU *cpu;
  int q;

  cpu = get_cpu();
  current = get_current_thread();

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
    next = cpu->idle_thread;
  }

  if (next != NULL) {
    next->state = THREAD_STATE_RUNNING;
    
    next_proc = get_thread_process(next);
    current_proc = get_current_process();
    pmap_switch(next_proc, current_proc);
  }

  if (next != current) {
    current->context = &context;
    cpu->current_thread = next;
    cpu->current_process = next->process;
    
    if (SetContext(&context[0]) == 0) {
      GetContext(next->context);
    }
  }
}

/* @brief   Add thread to a ready queue based on its scheduling policy and priority.
 */
void SchedReady(struct Thread *thread)
{
  struct Thread *next;
  struct CPU *cpu;

  cpu = get_cpu();

  if (thread->sched_policy == SCHED_RR || thread->sched_policy == SCHED_FIFO) {
    CIRCLEQ_ADD_TAIL(&sched_queue[thread->priority], thread, sched_entry);
    sched_queue_bitmap |= (1 << thread->priority);

  } else if (thread->sched_policy == SCHED_OTHER) {
    CIRCLEQ_ADD_TAIL(&sched_queue[thread->priority], thread, sched_entry);
    sched_queue_bitmap |= (1 << thread->priority);
  } else if (thread->sched_policy == SCHED_IDLE) {
    // Ignore IDLE sched policy, it's never placed in a sched queue
  } else {
    Error("Ready: Unknown sched policy %d", thread->sched_policy);
    KernelPanic();
  }

  thread->quanta_used = 0;
  cpu->reschedule_request = true;
}


/* @brief   Removes thread from the ready queue.
 */
void SchedUnready(struct Thread *thread)
{
  struct CPU *cpu;

  cpu = get_cpu();

  if (thread->sched_policy == SCHED_RR || thread->sched_policy == SCHED_FIFO) {
    CIRCLEQ_REM_ENTRY(&sched_queue[thread->priority], thread, sched_entry);
    if (CIRCLEQ_HEAD(&sched_queue[thread->priority]) == NULL) {
      sched_queue_bitmap &= ~(1 << thread->priority);
    }

    thread->quanta_used = 0;
    
  } else if (thread->sched_policy == SCHED_OTHER) {
    CIRCLEQ_REM_ENTRY(&sched_queue[thread->priority], thread, sched_entry);
    if (CIRCLEQ_HEAD(&sched_queue[thread->priority]) == NULL) {
      sched_queue_bitmap &= ~(1 << thread->priority);
    }

    thread->priority = thread->desired_priority;
    thread->quanta_used = 0;
  } else if (thread->sched_policy == SCHED_IDLE) {
    // Ignore IDLE sched policy, it's never placed in a sched queue
  } else {
    Error("Unready: Unknown sched policy *****");
    KernelPanic();
  }

  cpu->reschedule_request = true;
}


/* @brief   Set thread scheduling policy and priority
 */
int sys_setschedparams(int policy, int priority)
{
  struct Process *current_proc;
  struct Thread *current;
  int_state_t int_state;
  
  current_proc = get_current_process();
  current = get_current_thread();

  if (policy == SCHED_RR || policy == SCHED_FIFO) {
    if (!io_allowed(current_proc)) {
      return -EPERM;
    }

    if (priority < 16 || priority > 31) {
      return -EINVAL;
    }

    int_state = DisableInterrupts();
    SchedUnready(current);
    current->sched_policy = policy;
    current->desired_priority = priority;
    current->priority = priority;
    SchedReady(current);
    Reschedule();
    RestoreInterrupts(int_state);

    return 0;

  } else if (policy == SCHED_OTHER) {
    if (priority < 0 || priority > 15) {
      return -EINVAL;
    }

    int_state = DisableInterrupts();
    SchedUnready(current);
    current->sched_policy = policy;
    current->desired_priority = priority;
    current->priority = priority;
    SchedReady(current);
    Reschedule();
    RestoreInterrupts(int_state);

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


/* @brief   Helper function to initially schedule a thread
 * 
 */
void thread_start(struct Thread *thread)
{
  int_state_t int_state;
  
  int_state = DisableInterrupts();
  thread->state = THREAD_STATE_READY;
  SchedReady(thread);
  RestoreInterrupts(int_state);
}


/*
 *
 */
void thread_stop(void)
{
  struct Thread *thread;
  struct Thread *current_thread;
  int_state_t int_state;
  
  current_thread = get_current_thread();
  
  int_state = DisableInterrupts();

  thread = LIST_HEAD(&bkl_blocked_list);

  if (thread != NULL) {
    LIST_REM_HEAD(&bkl_blocked_list, blocked_link);
    thread->state = THREAD_STATE_READY;
    bkl_owner = thread;
    SchedReady(thread);
  } else {
    bkl_locked = false;
    bkl_owner = NULL;
  }

  current_thread->state = THREAD_STATE_EXITED;
  SchedUnready(current_thread);
  Reschedule();
  
  KernelPanic();
  while(1);
}


/*
 *
 */
int init_schedparams(struct Thread *thread, int policy, int priority)
{
  if (priority < 0) {
    priority = 0;
  }
  
  if (priority > 31) {
    priority = 31;
  }
  
  if (policy == SCHED_RR || policy == SCHED_FIFO) {
    thread->quanta_used = 0;
    thread->sched_policy = policy;
    thread->priority = (priority >= 16) ? priority : 16;
    thread->desired_priority = thread->priority;
  } else if (policy == SCHED_OTHER) {
    thread->quanta_used = 0;
    thread->sched_policy = policy;
    thread->priority = (priority < 16) ? priority : 0;
    thread->desired_priority = thread->priority;
  } else if (policy == SCHED_IDLE) {
    thread->quanta_used = 0;
    thread->sched_policy = SCHED_IDLE;
    thread->priority = 0;
    thread->desired_priority = 0;
  } else {
    Error("Unsupported kernel task sched policy %d", thread->sched_policy);
    KernelPanic();
  }
  
  return 0;
}


/*
 *
 */ 
int dup_schedparams(struct Thread *thread, struct Thread *old_thread)
{
  thread->quanta_used = 0;
  thread->sched_policy = old_thread->sched_policy;
  thread->priority = old_thread->priority;
  thread->desired_priority = old_thread->desired_priority;
  return 0;
}


