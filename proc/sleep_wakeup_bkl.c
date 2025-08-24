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
 * Big kernel lock and sleep and wakeup condition variable functions.
 * Currently the kernel is non-preeemptive due to a big kernel lock
 * acquired on kernel entry and released on kernel exit.  This forms
 * a "monitor" around the whole kernel, just like early Unix systems. 
 *
 * The main synchronization primitive is a rendez or condition-variable
 * That allows tasks in the kernel to sleep, waiting for a condition to
 * become true or wake up other tasks as a hint that the condition may
 * now be true.  All other synchronization is based on this.
 *
 * See Maurice Bach's "Design of the UNIX Operating System" book
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/msg.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/arch.h>


// Static prototypes
static void TaskTimedSleepCallback(struct Timer *timer);


/* @brief   Lock the Big Kernel Lock
 *
 * Big Kernel Lock acquired on kernel entry. Effectively coroutining
 * (co-operative multitasking) within the kernel.  Similar to the mutex used in
 * a condition variable construct.  TaskSleep and TaskWakeup are used to sleep
 * and wakeup tasks blocked on a condition variable (rendez).
 * 
 * Interrupts are disabled upon entry to a syscall in the assembly code.
 *
 * TODO: Make this a recurive, counting lock. So that we don't need to 
 * special case calling this for interrupts and exceptions.
 */
void KernelLock(void)
{
  struct Thread *current;

  current = get_current_thread();

  if (bkl_locked == false) {
    bkl_locked = true;
    bkl_owner = current;
  } else {
    LIST_ADD_TAIL(&bkl_blocked_list, current, blocked_link);
    current->state = THREAD_STATE_BKL_BLOCKED;
    SchedUnready(current);
    Reschedule();
  }
}


/* @brief   Unlock the Big Kernel Lock
 *
 * See comments above for KernelLock()
 *
 * This is called at the end of a system call, interrupt or exception. This checks to see
 * if there are any remaining processes blocked on the big kernel lock and if so
 * yields to one of these processes.
 *
 * Only when there are no other processes blocked on the big kernel lock do we
 * return to user mode.  
 *
 * We do not have kernel preemption, effectively all processes blocked on the BKL
 * must run before we can return to user space.
 *
 * TODO: We need to be on the caller's kernel stack (not the interrupt or exception stacks ???).
 */
void KernelUnlock(void)
{
  struct Thread *thread;

  if (bkl_locked == true) {
    thread = LIST_HEAD(&bkl_blocked_list);   // Pick the next thread that is blocked on bkl

    if (thread != NULL) {
      bkl_locked = true;      // It should be locked already by previous if statement
      bkl_owner = thread;

      LIST_REM_HEAD(&bkl_blocked_list, blocked_link);

      thread->state = THREAD_STATE_READY;
      SchedReady(thread);       // 
      Reschedule();
    } else {
      bkl_locked = false;
      bkl_owner = (void *)NULL;
    }
  } else {
    KernelPanic();
  }
}


/* @brief   Initialize a Rendez condition variable
 *
 * @param   rendez, condition variable to initialize
 */
void InitRendez(struct Rendez *Rendez)
{ 
  LIST_INIT(&Rendez->blocked_list);
}


/* @brief   Sleep on a Rendez condition variable
 *
 * @param   rendez, condition variable to sleep on
 */
void TaskSleep(struct Rendez *rendez)
{
  struct Thread *thread;
  struct Thread *current;
  int_state_t int_state;
    
  current = get_current_thread();

  int_state = DisableInterrupts();

  KASSERT(bkl_locked == true);
  KASSERT(bkl_owner == current);
  
  thread = LIST_HEAD(&bkl_blocked_list);

  if (thread != NULL) {
    LIST_REM_HEAD(&bkl_blocked_list, blocked_link);
    thread->state = THREAD_STATE_READY;
    bkl_owner = thread;
    SchedReady(thread);
  } else {
    bkl_locked = false;
    bkl_owner = (void *)NULL;
  }

  LIST_ADD_TAIL(&rendez->blocked_list, current, blocked_link);
  current->state = THREAD_STATE_RENDEZ_BLOCKED;
  current->blocking_rendez = rendez;
  SchedUnready(current);
  Reschedule();
  
  KASSERT(bkl_locked == true);
  KASSERT(bkl_owner == current);

  RestoreInterrupts(int_state);
}


/* @brief   Sleep on a Rendez condition variable with a timeout. (interruptible)
 *
 * @param   rendez, condition variable to sleep on
 * @param   ts, timeout to wake up after if the rendez was not signalled
 * @return  0 on success
 *          -EINTR if an event or signal is pending
 *          -ETIMEDOUT if a timeout occured
 *          other negative errno on failure
 *
 * TODO: Merge TaskSleepInterruptible, allow timeout to be optional
 */
int TaskSleepInterruptible(struct Rendez *rendez, struct timespec *ts, uint32_t intr_flags)
{
  struct Thread *thread;
  struct Thread *current;
  struct Timer *timer;
  uint64_t now;
  int_state_t int_state;
  int sc;
  
  current = get_current_thread();

  int_state = DisableInterrupts();

  KASSERT(bkl_locked == true);
  KASSERT(bkl_owner == current);

  if (TaskCheckInterruptible(current, intr_flags) != 0) {
    RestoreInterrupts(int_state);
    return -EINTR;
  }

  thread = LIST_HEAD(&bkl_blocked_list);

  if (thread != NULL) {
    LIST_REM_HEAD(&bkl_blocked_list, blocked_link);
    thread->state = THREAD_STATE_READY;
    bkl_owner = thread;
    SchedReady(thread);
  } else {
    bkl_locked = false;
    bkl_owner = (void *)NULL;
  }
  
  if (ts != NULL) {
    timer = &current->sleep_timer;  
    timer->thread = current;
    timer->arg = rendez;
    timer->armed = true;
    timer->callback = TaskTimedSleepCallback;

    now = get_hardclock();  
    timer->expiration_time = now + (ts->tv_sec * JIFFIES_PER_SECOND + ts->tv_nsec / NANOSECONDS_PER_JIFFY);
    LIST_ADD_TAIL(&timing_wheel[timer->expiration_time % JIFFIES_PER_SECOND], timer, timer_entry);
  }  
  
  LIST_ADD_TAIL(&rendez->blocked_list, current, blocked_link);
  current->state = THREAD_STATE_RENDEZ_BLOCKED;

  current->intr_flags = intr_flags;
  current->blocking_rendez = rendez;
  SchedUnready(current);
  Reschedule();

  current->intr_flags = 0;

  sc = TaskCheckInterruptible(current, intr_flags);
  
  if (ts != NULL) {
    if (timer->armed == true) {
      LIST_REM_ENTRY(&timing_wheel[timer->expiration_time % JIFFIES_PER_SECOND], timer, timer_entry);
      timer->armed = false;
      timer->thread = NULL;
      timer->callback = NULL;
    } else if (sc == 0) {
      sc = -ETIMEDOUT;
    }
  }
  
  RestoreInterrupts(int_state);
  return sc;
}


/* @brief   Check for pending events and signals
 *
 * @param   thread, thread to check
 * @return  0 if no signals or events pending, -EINTR otherwise
 */
int TaskCheckInterruptible(struct Thread *thread, uint32_t intr_flags)
{
  if (intr_flags & INTRF_EVENT) {  
    if((thread->pending_events & thread->event_mask) != 0) {
      return -EINTR;
    }
  }

#if 0  // FIXME: Allow signals to interrupt TaskSleepInterruptible
  if (intr_flags & INTRF_SIGNAL) {  
    if((thread->signal.sig_pending & ~thread->signal.sig_mask) != 0 ) {
      Info("signal interrupt %08x", (thread->signal.sig_pending & ~thread->signal.sig_mask));
      return -EINTR;
    }
  }
#endif

  return 0;
}


/* @brief   Timeout handler of task blocked by TaskTimedSleep()
 *
 * @param   timer, the timeout timer state created by TaskTimedSleep().
 *
 * This is called from the timer bottom half thread, so has the BKL
 * locked until it does a sleep
 */
static void TaskTimedSleepCallback(struct Timer *timer)
{
  struct Thread *thread = timer->thread;
  struct Rendez *rendez = timer->arg;
  int_state_t int_state;
  
  int_state = DisableInterrupts();

  KASSERT(thread->blocking_rendez == rendez);

  if (thread != NULL && thread->state == THREAD_STATE_RENDEZ_BLOCKED) {
    LIST_REM_ENTRY(&rendez->blocked_list, thread, blocked_link);
    thread->blocking_rendez = NULL;
    LIST_ADD_TAIL(&bkl_blocked_list, thread, blocked_link);
    thread->state = THREAD_STATE_BKL_BLOCKED;
  }
  
  RestoreInterrupts(int_state);
}


/* @brief   Wakeup a single task waiting on a condition variable
 *
 * @param   rendez, the condition variable to wake up a thread from
 */
void TaskWakeup(struct Rendez *rendez)
{
  struct Thread *thread;
  int_state_t int_state;
  
  int_state = DisableInterrupts();

  thread = LIST_HEAD(&rendez->blocked_list);

  if (thread != NULL) {
    KASSERT(thread->blocking_rendez == rendez);   
    KASSERT(thread->state == THREAD_STATE_RENDEZ_BLOCKED);   

    LIST_REM_HEAD(&rendez->blocked_list, blocked_link);
    thread->blocking_rendez = NULL;
    LIST_ADD_TAIL(&bkl_blocked_list, thread, blocked_link);
    thread->state = THREAD_STATE_BKL_BLOCKED;
  }

  RestoreInterrupts(int_state);
}


/* @brief   Wakeup a specific task if it is waiting on a condition variable
 *
 * @param   thread, thread to wakeup
 */
void TaskWakeupSpecific(struct Thread *thread, uint32_t intr_reason)
{
  int_state_t int_state;
  struct Rendez *rendez;
  
  int_state = DisableInterrupts();

  if (thread != NULL && thread->state == THREAD_STATE_RENDEZ_BLOCKED &&
      ((thread->intr_flags & intr_reason) != 0 || intr_reason == 0)) {    // FIXME: intr_reason confusing
    KASSERT(thread->blocking_rendez != NULL);
    rendez = thread->blocking_rendez;
    LIST_REM_ENTRY(&rendez->blocked_list, thread, blocked_link);
    thread->blocking_rendez = NULL;
    LIST_ADD_TAIL(&bkl_blocked_list, thread, blocked_link);
    thread->state = THREAD_STATE_BKL_BLOCKED;
  }

  RestoreInterrupts(int_state);
}


/*
 *
 */
void TaskRendezRequeue(struct Thread *thread, struct Rendez *r_new, struct Rendez *r_old)
{
  LIST_REM_HEAD(&r_old->blocked_list, blocked_link);
  LIST_ADD_TAIL(&r_new->blocked_list, thread, blocked_link);
  thread->blocking_rendez = r_new;
}


/* @brief   Wakeup all tasks waiting on a condition variable
 *
 * @param   rendez, the condition variable to wake up all tasks from
 */
void  TaskWakeupAll(struct Rendez *rendez)
{
  struct Thread *thread;
  int_state_t int_state;
  
  do {
    int_state = DisableInterrupts();

    thread = LIST_HEAD(&rendez->blocked_list);

    if (thread != NULL) {
      KASSERT(bkl_locked == true);
      
      LIST_REM_HEAD(&rendez->blocked_list, blocked_link);
      thread->blocking_rendez = NULL;
      LIST_ADD_TAIL(&bkl_blocked_list, thread, blocked_link);
      thread->state = THREAD_STATE_BKL_BLOCKED;
    }

    thread = LIST_HEAD(&rendez->blocked_list);
    RestoreInterrupts(int_state);

  } while (thread != NULL);
}



