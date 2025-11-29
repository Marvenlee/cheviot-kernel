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
 * Shared-Exclusive reader-writer locks.
 *
 * The reader-writer locks are used in conjunction with the big kernel lock
 * and can be considered an extension of the Rendez/condition-variables.
 * The big kernel lock allows only a single thread into the kernel and these
 * threads normally block using TaskSleep() on a condition variable, at which
 * point the thread cooperatively yields to another thread.
 *
 * The reader-writer locks work in much the same way.  There is still the
 * big kernel lock to ensure only a single task is running in the kernel, but
 * the rwlocks extend on the condition variables to allow multiple readers
 * or a writer into a section of code. If the lock is in a shared state
 * with multiple readers then these readers are cooperatively scheduled whenever
 * one of these reader threads blocks.
 *
 * See fs/vnode.c for table of rwlock usage among the filesystem system calls  
 */

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/msg.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/arch.h>


/* @brief   Acquire exclusive-access to item guarded by rwlock
 *
 */
int rwlock_exclusive(struct RWLock *lock)
{
#if 0
  while (lock->is_draining == false  && (lock->exclusive_cnt != 0 || lock->share_cnt != 0)) {
    Info("lock->exclusive_cnt = %d, share_cnt = %d, sleeping", lock->exclusive_cnt, lock->share_cnt);
    TaskSleep(&lock->rendez);
  }

  if (lock->is_draining == true) {
    return -EINVAL;
  }
  
  lock->exclusive_cnt = 1;
#endif

  return 0;
}


/* @brief   Acquire shared-access to item guarded by rwlock
 *
 */
int rwlock_shared(struct RWLock *lock)
{
#if 0
  while (lock->is_draining == false && lock->exclusive_cnt == 1) {
    TaskSleep(&lock->rendez);
  }

  if (lock->is_draining == true) {
    return -EINVAL;
  }
  
  lock->share_cnt++;
#endif
  return 0;
}


/* @brief   Upgrade from shared-access to exclusive-access of item guarded by rwlock
 *
 */
int rwlock_upgrade(struct RWLock *lock)
{
#if 0
  KASSERT(lock->exclusive_cnt == 0);
  KASSERT(lock->share_cnt != 0);

  while(lock->is_draining == false && (lock->share_cnt != 1 || lock->exclusive_cnt != 0)) {
    TaskSleep(&lock->rendez);
  }

  if (lock->is_draining == true) {
    return -EINVAL;
  }

  KASSERT(lock->share_cnt == 1);
  KASSERT(lock->exclusive_cnt == 0);

  lock->share_cnt = 0;
  lock->exclusive_cnt = 1;
#endif
  return 0;
}  


/* @brief   Downgrade from exclusive-access to shared-access of item guarded by rwlock
 *
 */
void rwlock_downgrade(struct RWLock *lock)
{
#if 0
  KASSERT(lock->exclusive_cnt != 1);
  KASSERT(lock->share_cnt == 0);
  
  lock->exclusive_cnt = 0;
  lock->share_cnt = 1;
#endif
}  


/* @brief   Release rwlock
 *
 */
void rwlock_release(struct RWLock *lock)
{
#if 0
  if (lock->share_cnt > 0) {
    lock->share_cnt--;
  } else if (lock->exclusive_cnt == 1) {
    lock->exclusive_cnt = 0;
  }
  
  if (lock->exclusive_cnt == 0 || lock->share_cnt == 0) {
    TaskWakeupAll(&lock->rendez);
  }
#endif
}  


/* @brief   Deny other threads from acquiring the rwlock and wait for rwlock to be released. 
 *
 */
int rwlock_drain(struct RWLock *lock)
{
#if 0
  if (lock->is_draining == true) {
    return -EINVAL;
  }
  
  lock->is_draining = true;
  while(lock->exclusive_cnt != 0 && lock->share_cnt != 0) {
    TaskSleep(&lock->rendez);
  }

#endif
  return 0;
}  


/* @brief   RWLock initialization
 *
 */
void rwlock_init(struct RWLock *lock)
{
  lock->is_draining = false;
  lock->share_cnt = 0;
  lock->exclusive_cnt = 0;
  InitRendez(&lock->rendez);  
}


/* @brief   RWLock reset from draining state
 *
 */
void rwlock_reset(struct RWLock *lock)
{
  KASSERT(lock->is_draining = true);
  
  lock->is_draining = false;
  lock->share_cnt = 0;
  lock->exclusive_cnt = 0;
  InitRendez(&lock->rendez);  
}

