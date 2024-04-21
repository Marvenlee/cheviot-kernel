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
 * Architecture-neutral code to create and close InterruptHandler objects
 * Used by device drivers to receive notification of interrupts.
 */

#define KDEBUG 1

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>



/*
 *
 */
uint32_t sys_thread_event_check(uint32_t event_mask)
{
  struct Process *current;
  int sc = 0;
  int_state_t int_state;
  uint32_t caught_events;
  
  Info("sys_thread_event_check");
  
  current = get_current_process();  // FIXME: Replace with threads

  int_state = DisableInterrupts();
  caught_events = current->pending_events & event_mask;
  current->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);
  
  return caught_events;
}


/*
 *
 */
uint32_t sys_thread_event_wait(uint32_t event_mask)
{
  struct Process *current;
  int_state_t int_state;
  uint32_t caught_events;
  
  current = get_current_process();  // FIXME: Replace with threads
  
  if ((current->pending_events & event_mask) == 0) {
    TaskSleepInterruptible(&current->rendez);   // FIXME: Check result
                                                     // Should we loop ?
                                                     // Should we exit if pending signals?
  }
  
  int_state = DisableInterrupts();
  caught_events = current->pending_events & event_mask;
  current->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);
  
  return caught_events;
}


/*
 *
 */
int sys_thread_event_signal(int thread_id, int event)
{
  struct Process *proc;
  int_state_t int_state;
    
  proc = get_current_process(); // FIXME: Replace with thread IDS when threads are supported.
  
  if (proc == NULL) {
    return -EINVAL;
  }

  int_state = DisableInterrupts();
  proc->pending_events |= (1<<event);
  RestoreInterrupts(int_state);

  TaskWakeupSpecific(proc);
}


/*
 *
 * Interrupts should be disabled when calling this function.
 */
int isr_thread_event_signal(int thread_id, int event)
{
  struct Process *proc;
  
  proc = GetProcess(thread_id);  // FIXME: Replace with thread IDs when threads are supported.
  
  if (proc == NULL) {
    return -EINVAL;
  }

  proc->pending_events |= (1<<event);
  TaskWakeupSpecific(proc);
}

