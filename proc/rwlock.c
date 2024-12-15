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



/* @brief   rw lock acquisition and release
 *
 */
int rwlock(struct RWLock *lock, int flags)
{
  int request;

  request = flags & LOCK_REQUEST_MASK;
  
  switch(request) {
    case LK_EXCLUSIVE:
      if (lock->is_draining == true) {
        return -EINVAL;
      }

      while (lock->exclusive_cnt != 0 || lock->share_cnt != 0) {
        TaskSleep(&lock->rendez);
      }

      if (lock->is_draining == true) {
        return -EINVAL;
      }
      
      lock->exclusive_cnt = 1;
      break;

    case LK_SHARED:
      if (lock->is_draining == true) {
        return -EINVAL;
      }

      while (lock->exclusive_cnt == 1) {
        TaskSleep(&lock->rendez);
      }

      if (lock->is_draining == true) {
        return -EINVAL;
      }
      
      lock->share_cnt++;
      break;

    case LK_UPGRADE:
      if (lock->is_draining == true) {
        return -EINVAL;
      }

      if (lock->exclusive_cnt == 1) {
        return -EINVAL;
      }
    
      if (lock->share_cnt > 0) {
        lock->share_cnt--;
      }
            
      while(lock->share_cnt != 0 || lock->exclusive_cnt != 0) {
        TaskSleep(&lock->rendez);
      }

      if (lock->is_draining == true) {
        return -EINVAL;
      }

      lock->exclusive_cnt = 1;
      break;

    case LK_DOWNGRADE:
      if (lock->exclusive_cnt == 1) {
        lock->exclusive_cnt = 0;
        lock->share_cnt++;
      } else {
        return -EINVAL;
      }            
      break;

    case LK_RELEASE:
      if (lock->share_cnt > 0) {
        lock->share_cnt--;
      } else if (lock->exclusive_cnt == 1) {
        lock->exclusive_cnt = 0;
      }
      
      if (lock->exclusive_cnt == 0 || lock->share_cnt == 0) {
        TaskWakeupAll(&lock->rendez);
      }
      break;

    case LK_DRAIN:
      if (lock->is_draining == true) {
        return -EINVAL;
      }
      
      lock->is_draining = true;
      while(lock->exclusive_cnt != 0 && lock->share_cnt != 0) {
        TaskSleep(&lock->rendez);
      }
      break;

    default:
      return -EINVAL;      
  }
  
  return 0;
}


/* @brief   VNode lock initialization
 *
 */
int rwlock_init(struct RWLock *lock)
{
  lock->is_draining = false;
  lock->share_cnt = 0;
  lock->exclusive_cnt = 0;
  InitRendez(&lock->rendez);  
  return 0;
}

