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
#include <sys/privileges.h>
#include <pthread.h>
#include <sys/_pthreadtypes.h>


/* @brief   Set thread scheduling policy and priority
 */
int sys_thread_setschedparams(int policy, int priority)
{
  struct Process *current_proc;
  struct Thread *current;
  int_state_t int_state;
  
  Info("sys_setschedparams(policy:%d, priority:%d", policy, priority);
  
  current_proc = get_current_process();
  current = get_current_thread();

  if (policy == SCHED_RR || policy == SCHED_FIFO) {
    if (check_privileges(current_proc, PRIV_SCHED_RR | PRIV_SCHED) != 0) {
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


/* @brief   Set thread scheduling policy and priority
 */
int sys_thread_getschedparams(pid_t tid, int *policy, int *priority)
{
  return -ENOSYS;
}


/*
 *
 */
int sys_thread_getpriority(pid_t tid)
{
	return 0;
}


/*
 *
 */
int sys_thread_setpriority(pid_t tid)
{
	return 0;
}




