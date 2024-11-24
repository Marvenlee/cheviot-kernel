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
 * Thread CPU usage monitoring (for top command)
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/msg.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/arch.h>
#include <kernel/timer.h>
#include <unistd.h>
#include <sys/resource.h>
#include <string.h>


/*
 *
 */
int sys_get_cpu_usage(void *buf, size_t sz)
{
  struct Thread *thread;
  struct Process *proc;
  struct cpu_usage cu;
  struct cpu_usage *rcu;
  int max_cu;
  int cnt = 0;
  
  Info("sys_get_cpu_usage(buf:%08x, sz:%u)", (uint32_t)buf, sz);
  
  uint64_t now_usec;
  
  rcu = (struct cpu_usage *)buf;
  max_cu = sz / sizeof (struct cpu_usage);

  now_usec = arch_get_monotonic_usec();

  for (int t=0; t<max_pid && cnt < max_cu; t++) {
    thread = get_thread(t);
    
    Info("pid: %d, thread:%08x", t, (uint32_t)thread);
    
    if (thread == NULL) {
      continue;
    }
    
    proc = thread->process;

    Info("thread->process = %08x", (uint32_t)proc);
    
    KASSERT(proc != NULL);
    
    // Need to set usage_start_usec and creation_usec on thread creation.
    
    cu.tid = thread->tid;
//    cu.pid = proc->pid;
    cu.uid = proc->uid;
    cu.uptime_sec = (now_usec - thread->creation_usec) / 1000000;
    
    uint64_t diff_usec;
    
//    if (thread->creation_usec > usage_start_usec) {
//      diff_usec = now_usec - thread->creation_usec;
//    } else {
      diff_usec = now_usec - usage_start_usec;    
//    }
    
    if (diff_usec == 0) {
      diff_usec = 1000000;
    }
    
    cu.usage_permille = (1000 * thread->usage_usec) / diff_usec;
   
    cu.cpu = 0;
    cu.priority = thread->priority;
    cu.policy = thread->sched_policy;
    
    strncpy(&cu.name[0], &thread->basename[0], sizeof cu.name);
      
    // Reset counters
    thread->usage_usec = 0;
    
    CopyOut(rcu, &cu, sizeof cu);

    rcu++;
    cnt++;
  }

  usage_start_usec = now_usec;

  return cnt;
}




