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

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <poll.h>
#include <sys/select.h>
#include <string.h>

KLOG_REGISTER(LOG_FS_POLL)


/*
 *
 */
int sys_poll(struct pollfd *_fds, nfds_t nfds, int timeout)
{
  static const int backoff_ticks[] = {1, 1, 2, 3, 5, 8, 13, 21};
  struct pollfd pfd;
  uint64_t now;
  uint64_t expiration_time;
  int found = 0;
  int retry = 0;
  int sc;
  struct Process *current;
  struct Filp *filp;
  struct VNode *vnode;
  
  current = get_current_process();

  if (timeout != 0) {    
    now = get_hardclock();
    expiration_time = now + timeout;  // FIXME: Convert to milliseconds or seconds?
  }
  
  while (1) {
    for(int t=0; t < nfds; t++) {
      sc = copyin(&pfd, &_fds[t], sizeof pfd);

      if (sc != 0) {
        return sc;
      }

      pfd.revents = 0;
            
      filp = filp_get(current, pfd.fd);
      
      if (filp) {
        vnode = vnode_get_from_filp(filp);
        
        if (vnode) {
           sc = vfs_poll(vnode, pfd.events, &pfd.revents);
           vnode_put(vnode);
        }
      }

      if (pfd.revents != 0) {
        found++;
      }
      
      sc = copyout(&_fds[t], &pfd, sizeof pfd);

      if (sc != 0) {
        return sc;
      }       
    }

    if (found > 0) {
      break;
    }

    if (timeout != 0) {
      now = get_hardclock();
      
      if (now >= expiration_time) {
        return -ETIMEDOUT;
      }
      
      if (retry >= (sizeof backoff_ticks / sizeof backoff_ticks[0]) - 1) {
        retry = (sizeof backoff_ticks / sizeof backoff_ticks[0]) - 1;
      }
            
      sleep_ticks(backoff_ticks[retry]);
    }
    
    retry++;
  }
  
  return found;
}


