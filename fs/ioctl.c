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

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <poll.h>
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <termios.h>


/*
 *
 */
int sys_ioctl(int fd, int cmd, intptr_t arg)
{
  int sc;
  struct Filp *filp;
  struct VNode *vnode;
  struct Process *current;
  
  Info("sys_ioctl(fd:%d, cmd:%d", fd, cmd);
  
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    Error("iotctl - FD invalid, no vnode");
    return -EINVAL;
  }
    
  if (!S_ISCHR(vnode->mode)) {
    Error("iotctl - FD invalid, not char");
    return -EINVAL;
  }

  vn_lock(vnode, VL_EXCLUSIVE);
  
  switch (cmd)
  {
    case TCSETS:
      sc = -ENOTSUP;
      break;

    case TCSETSW:
      sc = -ENOTSUP;
      break;

    case TCSETSF:
      sc = -ENOTSUP;
      break;

    case TCGETS:
      sc = -ENOTSUP;
      break;

    case TIOCGSID:
      sc = ioctl_tiocgsid(fd, (pid_t *)arg);
      break;

    case TIOCGPGRP:
      sc = ioctl_tiocgpgrp(fd, (pid_t *)arg);
      break;

    case TIOCSPGRP:     
      sc = ioctl_tiocspgrp(fd, (pid_t *)arg);
      break;

    case TCXONC:
      sc = -ENOTSUP;
      break;

    case TCFLSH:
      sc = -ENOTSUP;
      break;

    case TIOCSCTTY:
      sc = ioctl_tiocsctty(fd, arg);
      break;
      
    case TIOCNOTTY:
      sc = ioctl_tiocnotty(fd);
      break;
      
    case IOCTL_SETSYSLOG:
      sc = -ENOTSUP;        // TODO: Redirect logging output here.
      break;        
      
    default:
      sc = -ENOTSUP;
      break;
  }

  vn_lock(vnode, VL_RELEASE);

  Error("iotctl - returned %d", sc);
  return sc;
}


