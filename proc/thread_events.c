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

//#define KDEBUG 1

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/kqueue.h>

/* @brief   Allow thread events to wake up a thread blocked on kevent().
 *
 * This will apply to ANY kqueue we have registered with.
 */
 
int sys_thread_event_kevent_mask(int kq, uint32_t event_mask)
{
  struct KQueue *kqueue;
  struct Thread *cthread;
  struct Process *cproc;
  
  int sc = 0;
  struct kevent ev;
  
  ev.ident = get_current_tid();  
  ev.filter = EVFILT_THREAD_EVENT;          
  ev.flags = 0;
  ev.fflags = 0;
  ev.data = NULL;
  ev.udata = NULL;
    
  Info("sys_thread_event_kevent_mask(%08x)", event_mask);
  
  cproc = get_current_process();
  cthread = get_current_thread();
  
  cthread->kevent_event_mask = event_mask;
  
  if (cthread->event_knote == NULL) {
    kqueue = get_kqueue(cproc, kq);
  
    if (kqueue == NULL) {
      return -EINVAL;
    }
  
    while (kqueue->busy == true) {
      TaskSleep(&kqueue->busy_rendez);
    }
    kqueue->busy = true;

    cthread->event_knote = alloc_knote(kqueue, &ev);
    
    if (cthread->event_knote != NULL) {
      cthread->event_kqueue = kqueue;
      enable_knote(kqueue, cthread->event_knote);
    } else {
      cthread->kevent_event_mask = 0;
      cthread->event_kqueue = kqueue;
      sc = -ENOMEM;
    }

    kqueue->busy = false;
    TaskWakeup(&kqueue->busy_rendez);
  }
  
  return sc;
}


/*
 *
 */
uint32_t sys_thread_event_check(uint32_t event_mask)
{
  struct Thread *cthread;
  int sc = 0;
  int_state_t int_state;
  uint32_t caught_events;
  uint32_t new_pending_events;
  
  cthread = get_current_thread();
  
  int_state = DisableInterrupts();
  caught_events = cthread->pending_events & event_mask;
  cthread->pending_events &= ~caught_events;
  new_pending_events = cthread->pending_events;
  RestoreInterrupts(int_state);
  
  Info("sys_thread_event_check() caught:%08x remaining:%08x", caught_events, new_pending_events);
  
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
int sys_thread_event_signal(int tid, int event)
{
  struct Thread *thread;
  struct Thread *cthread;
  int_state_t int_state;
    
  cthread = get_current_thread();
  thread = get_thread(tid);
  
  if (thread == NULL || thread->process != cthread->process) {
    return -EPERM;
  }

  int_state = DisableInterrupts();

  thread->pending_events |= (1<<event);
  if ((thread->pending_events & thread->kevent_event_mask) != 0) {  
    TaskWakeupSpecific(thread, INTRF_EVENT);
  }
  
  RestoreInterrupts(int_state);


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
  if ((thread->pending_events & thread->kevent_event_mask) != 0) {  
    TaskWakeupSpecific(thread, INTRF_EVENT);
  }
  return 0;
}

