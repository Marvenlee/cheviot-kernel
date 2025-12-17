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

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>

KLOG_REGISTER(LOG_PROC_THREAD_EVENTS)


/*
 *
 */
uint32_t sys_thread_event_check(uint32_t event_mask)
{
  struct Thread *cthread;
  int_state_t int_state;
  uint32_t caught_events;
  
  cthread = get_current_thread();
  
  int_state = DisableInterrupts();
  caught_events = cthread->pending_events & event_mask;
  cthread->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);
    
  return caught_events;
}


/*
 *
 */
uint32_t sys_thread_event_wait(uint32_t event_mask)
{
  struct Thread *cthread;
  int_state_t int_state;
  uint32_t caught_events;
  
  cthread = get_current_thread();

  // Should interrupts be disabled for all of this?
  
  cthread->event_mask = event_mask;
  
  if ((cthread->pending_events & cthread->event_mask) == 0) {
    TaskSleepInterruptible(&cthread->rendez, NULL, INTRF_ALL);   // FIXME: Check sc result, could be signal or event
                                                     // Should we loop ?
                                                     // Should we exit if pending signals?
  }
  
  int_state = DisableInterrupts();
  caught_events = cthread->pending_events & cthread->event_mask;
  cthread->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);
  
  return caught_events;
}


/*
 *
 */
uint32_t sys_thread_event_timedwait(uint32_t event_mask, struct timespec *_timeout)
{
  struct Thread *cthread;
  int_state_t int_state;
  uint32_t caught_events;
  struct timespec timeout;
  struct timespec *timeoutp;
  
  cthread = get_current_thread();

  
  if (_timeout != NULL) {
    if (copyin(&timeout, _timeout, sizeof timeout) != 0) {
      return -EFAULT;
    }
    
    timeoutp = &timeout;
  } else {
    timeoutp = NULL;
  }
  
  // Should interrupts be disabled for all of this?
  
  cthread->event_mask = event_mask;
  
  if ((cthread->pending_events & cthread->event_mask) == 0) {
    TaskSleepInterruptible(&cthread->rendez, timeoutp, INTRF_ALL);   // FIXME: Check sc result, could be signal or event
                                                     // Should we loop ?
                                                     // Should we exit if pending signals?
  }
  
  int_state = DisableInterrupts();
  caught_events = cthread->pending_events & cthread->event_mask;
  cthread->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);
  
  return caught_events;
}


/*
 *
 */
int sys_thread_event_signal(int tid, int event)
{
  struct Thread *thread;
  struct Thread *cthread;
  int_state_t int_state;
    
  cthread = get_current_thread();
  thread = get_thread(tid);
  
  if (thread == NULL || thread->process != cthread->process) {
    klog_error("sys_thread_event_signal() -EPERM");
    return -EPERM;
  }

  if (event < 0 || event > 31) {
    return -EINVAL;
  }

  int_state = DisableInterrupts();

  thread->pending_events |= (1<<event);
  if ((thread->pending_events & thread->event_mask) != 0) {  
    TaskWakeupSpecific(thread, INTRF_EVENT);
  }
  
  RestoreInterrupts(int_state);

  return 0;
}


int do_thread_event_signal(int tid, int event)
{
  struct Thread *thread;
  int_state_t int_state;
    
  thread = get_thread(tid);
  
  if (thread == NULL) {
    klog_error("do_thread_event_signal() -ENOENT");
    return -ENOENT;
  }

  if (event < 0 || event > 31) {
    return -EINVAL;
  }

  int_state = DisableInterrupts();

  thread->pending_events |= (1<<event);
  if ((thread->pending_events & thread->event_mask) != 0) {  
    TaskWakeupSpecific(thread, INTRF_EVENT);
  }
  
  RestoreInterrupts(int_state);

  return 0;
}



/*
 *
 * Interrupts should be disabled when calling this function.
 */
int isr_thread_event_signal(struct Thread *thread, int event)
{
  if (thread == NULL) {
    return -EINVAL;
  }

  thread->pending_events |= (1<<event);
  if ((thread->pending_events & thread->event_mask) != 0) {  
    TaskWakeupSpecific(thread, INTRF_EVENT);
  }
  return 0;
}

