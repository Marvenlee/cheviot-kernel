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

KLOG_REGISTER(LOG_FS_SELECT)


/*
 *
 */
int sys_select(int nfds, fd_set *_rdfds, fd_set *_wrfds, fd_set *_exfds, struct timeval *_timeout)
{
  static const int backoff_ticks[] = {1, 1, 2, 3, 5, 8, 13, 21};
  fd_set rdfds;
  fd_set wrfds;
  fd_set exfds;
  struct timeval timeout;
  uint64_t now;
  uint64_t expiration_time;
  short events;
  short revents;
  int found = 0;
  int retry = 0;
  bool match = false;
  int sc;
  struct Process *current;
  struct Filp *filp;
  struct VNode *vnode;
  
  current = get_current_process();

  if (_rdfds != NULL) {
    sc = copyin(&rdfds, _rdfds, sizeof rdfds);
    if (sc != 0) {
      return sc;
    }    
  } else {
    FD_ZERO(&rdfds);
  }
  
  if (_wrfds != NULL) {
    sc = copyin(&wrfds, _wrfds, sizeof wrfds);
    if (sc != 0) {
      return sc;
    }    
  } else {
    FD_ZERO(&rdfds);
  }

  if (_exfds != NULL) {
    sc = copyin(&exfds, _exfds, sizeof exfds);
    if (sc != 0) {
      return sc;
    }    
  } else {
    FD_ZERO(&exfds);
  }

  if (_timeout != NULL) {
    sc = copyin(&timeout, _timeout, sizeof timeout);
    if (sc != 0) {
      return sc;
    }
    
    
    now = get_hardclock();
    expiration_time = now + timeval_to_ticks(&timeout);
  }
  
  
  while (1) {
    for(int t=0; t < nfds; t++) {
		  if (!(FD_ISSET(t, &rdfds) || FD_ISSET(t, &wrfds) || FD_ISSET(t, &exfds))) {
			  continue;
      }
      
      events = 0;
      
		  if (FD_ISSET(t, &rdfds)) {
		    events |= POLLIN;
      }
      
		  if (FD_ISSET(t, &wrfds)) {
		    events |= POLLOUT;
      }

		  if (FD_ISSET(t, &rdfds)) {
		    events |= ~(POLLIN | POLLOUT);
      }

      
      filp = filp_get(current, t);
      
      if (filp) {
        vnode = vnode_get_from_filp(filp);
        
        if (vnode) {
           sc = vfs_poll(vnode, events, &revents);
           vnode_put(vnode);
        } else {
          sc = -ENOENT;
          revents = 0;
        }
      } else {
        sc = -ENOENT;
        revents = 0;
      }

      match = false;

      if (revents != 0) {
        if (_rdfds != NULL && (revents & POLLIN)) {
          FD_SET(t, &rdfds);
          match = true;
        }
        
        if (_wrfds != NULL && (revents & POLLOUT)) {
          FD_SET(t, &wrfds);
          match = true;
        }
        
        if (_exfds != NULL && (revents & ~(POLLIN | POLLOUT))) {
          FD_SET(t, &exfds);
          match = true;
        }
        
        if (match == true) {
          found++;
        }
      }
    }

    if (found > 0) {
      break;
    }

    if (_timeout != NULL) {
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
    
  if (_rdfds != NULL) {
    sc = copyout(_rdfds, &rdfds, sizeof rdfds);
    if (sc != 0) {
      return sc;
    }
  }

  if (_wrfds != NULL) {
    sc = copyout(_wrfds, &wrfds, sizeof wrfds);
    if (sc != 0) {
      return sc;
    }
  }

  if (_exfds != NULL) {
    sc = copyout(_exfds, &exfds, sizeof exfds);
    if (sc != 0) {
      return sc;
    }
  }
  
  return found;
}


