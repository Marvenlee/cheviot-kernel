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
 * Signalling foreground process group from TTY device drivers
 */

//#define KDEBUG

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <kernel/proc.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/dbg.h>
#include <poll.h>


/* @brief   Send a signal to processes with a particular open file
 *
 * @param   fd, file descriptor of the mount point created by sys_mount()
 * @param   ino, inode number of TTY that the signal will be sent to
 * @param   signal, signal to raise
 *
 * This system call is intended for the TTY driver to be able to send
 * signals to client processes in response to keyboard inputs such
 * as SIGTERM, SIGKILL, etc.
 *
 * TODO: Mount() to explicitly set that it is a TTY
 */
int sys_signalnotify(int fd, int ino_nr, int signal)
{
  struct Process *current;
  struct VNode *target_vnode;
  struct Session *session;
  struct SuperBlock *sb;  
  int sc = 0;
  
  Info("sys_signalnotify(fd:%d, ino:%d, sig:%d)", fd, ino_nr, signal);
  
  current = get_current_process();  
  sb = get_superblock(current, fd);

  if (sb == NULL) {
    Error("sys_signalnotify -EBADF fd not msgport sb");
    return -EBADF;
  }
  
  target_vnode = vnode_find(sb, ino_nr);
  
  if (target_vnode == NULL) {
    Error("sys_signalnotify -EINVAL, ino_nr invalid");
    return -EINVAL;
  }

  if (S_ISCHR(target_vnode->mode)) {        
    if ((session = get_session(target_vnode->tty_sid)) != NULL) {
      if (session->foreground_pgrp != INVALID_PID) {
        sc = do_kill_process_group(session->foreground_pgrp, signal, 0, 0);
      } else {
        Error("No foreground pgrp -EBADF");
        sc = -EBADF;      
      }
    } else {
      Error("No session -EBADF");
      sc = -EBADF;
    }
    
  } else {
    Error("sys_signalnotify -EBADF not char");
    sc = -EBADF;
  }
  
  return sc;
}

