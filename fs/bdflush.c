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

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>


/* @brief   Create a kernel task to periodically flush a filesystem's dirty blocks
 * 
 */
int init_superblock_bdflush(struct SuperBlock *sb)
{
  Info("init_superblock_bdflush");

#if 0
  sb->bdflush_thread = create_kernel_thread(bdflush_task, sb, 
                                        SCHED_RR, SCHED_PRIO_CACHE_HANDLER, 
                                        THREADF_KERNEL, NULL, "bdflush-kt");
  
  if (sb->bdflush_thread == NULL) {
    Info("bd_flush initialization failed");
    return -ENOMEM;
  }
#endif
  
  return 0;
}


/* @brief   Shutdown the bdflush kernel task of a filesystem
 *
 * @param   sb, superblock of the bdflush task to stop
 * @param   how, option to control how the task is stopped
 *
 * TODO: Set how this should be shutdown, flush all or abort immediately.
 */
void fini_superblock_bdflush(struct SuperBlock *sb, int how)
{ 
  sb->flags |= SBF_ABORT;

#if 1  
  TaskWakeup(&sb->bdflush_rendez);

  if (sb->bdflush_thread != NULL) {
    do_join_thread(sb->bdflush_thread, NULL);
  }
  
  sb->bdflush_thread = NULL;
#endif
}


/* @brief   Per-Superblock kernel task for flushing async and delayed writes to disk
 *
 * @param   arg, pointer to the superblock
 */
void bdflush_task(void *arg)
{
	struct SuperBlock *sb;
  struct VNode *vnode;
  struct timespec timeout;
  
	sb = (struct SuperBlock *)arg;

  while((sb->flags & SBF_ABORT) == 0) {
    timeout.tv_sec = 4;
    timeout.tv_nsec = 0;    

    TaskSleepInterruptible(&sb->bdflush_rendez, &timeout, INTRF_NONE);

    Info("bdflush_task() sb->lock SHARED");
    
    rwlock(&sb->lock, LK_SHARED);    
                
    vnode = LIST_HEAD(&sb->vnode_list);
    
    while (vnode != NULL) {
      // FIXME: Do we need to increment reference count of vnode?

      bsyncv(vnode);

      vnode = LIST_NEXT(vnode, vnode_link);
    }

    Info("bdflush_task() sb->lock RELEASE");

    rwlock(&sb->lock, LK_RELEASE);
  }
}


