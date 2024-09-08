/*
 * Copyright 2014  Marven Gilhespie
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http: *www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


//#define KDEBUG

#include <kernel/types.h>
#include <kernel/proc.h>
#include <kernel/filesystem.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <kernel/dbg.h>
#include <kernel/board/boot.h>
#include <kernel/globals.h>
#include <sys/termios.h>
#include <unistd.h>


/* @brief   Read data from a character device
 *
 * TODO: Currently using a 512 byte buffer on the stack.  We need to eliminate the double
 * copy from the character driver to this buffer on a vfs_read and then from this buffer
 * to the client's user-space buffer.
 *
 * Could server-side readmsg/writemsg access the physical memory map (to avoid double copy).
 * Add CopyFromProcess(), CopyToProcess() to do this instead of copyin, copyout.
 *
 * Note: There is no use of vnode->busy.  Instead we use vnode->reader_cnt and vnode->writer_cnt
 * All other commands are assumed to be going to the command type queue.
 *
 * This code limits the sending of 1 write, 1 read and 1 command at a time.
 *
 * TODO: Need to handle non-blocking reads and writes
 *
 * TODO: Needs to be signal/event interruptible for character device reads, writes and ioctls
 *
 * How ?
 *
 * Send an ioctl-like third-man message to abort any reads or writes.  There should
 * only be a single reader and/or single writer blocked in char device. The rest
 * are blocked in here and can be aborted.
 *
 * OR
 *
 * Send a signal to the server SIGIOABORT ? to abort ALL current IO operations.
 * or split intio SIGIOABORT_READ, SIGIOABORT_WRITE and SIGIOABORT_IOCTL ?
 *
 * or send SIGIOABORT and have a getmsgportqueueinfo() to return which msgids to abort? 
 *
 */
ssize_t read_from_char(struct VNode *vnode, void *dst, size_t sz)
{
  uint8_t buf[512];
  ssize_t xfered = 0;
  size_t xfer = 0;
  struct Process *current;
  
  Info("read_from_char(dst:%08x, sz:%d", (uint32_t)dst, sz);
  
  current = get_current_process();
  
  while (vnode->reader_cnt != 0) {
    if (TaskSleepInterruptible(&vnode->rendez, NULL, INTRF_ALL) != 0) {
      Error("read_from_char reader_cnt -EINTR");
      return -EINTR;
    }    
  }

  vnode->reader_cnt = 1;
    
  xfer = (sz < sizeof buf) ? sz : sizeof buf;

  if (xfer > 0) {
    xfered = vfs_read(vnode, buf, xfer, NULL);
   
    if (xfered > 0) {  
      CopyOut (dst, buf, xfered);
    } else if (xfered == -EINTR) {
      Info("char vfs_read returned -EINTR");
    }
  }
  
  vnode->reader_cnt = 0;
  TaskWakeupAll(&vnode->rendez);

  Info("** read_from_char(sz:%d) xfered = %d", sz, xfered);

  return xfered;
}


/* @brief   Write data to a character device
 *
 */
ssize_t write_to_char(struct VNode *vnode, void *src, size_t sz)
{
  uint8_t buf[512];
  ssize_t xfered = 0;
  size_t xfer = 0;
  struct Process *current;

  Info("write_to_char(src:%08x, sz:%d)", (uint32_t)src, sz);

  current = get_current_process();

  while (vnode->writer_cnt != 0) {
    if (TaskSleepInterruptible(&vnode->rendez, NULL, INTRF_ALL) != 0) {
      Error("write_to_char writer_cnt -EINTR");
      return -EINTR;
    }    
  }

  vnode->writer_cnt = 1;
  xfer = (sz < sizeof buf) ? sz : sizeof buf;

  if (xfer > 0) {      
    CopyIn (buf, src, xfer);
    xfered = vfs_write(vnode, buf, xfer, NULL);
    
    if (xfered == -EINTR) {
      Info("char vfs_write returned -EINTR");
    }
  }
    
  vnode->writer_cnt = 0;
  TaskWakeupAll(&vnode->rendez);    

  Info("** write_to_char(sz:%d) xfered = %d", sz, xfered);

  return xfered;
}


/* @brief   Indicate if a file handle points to a TTY
 *
 */
int sys_isatty(int fd)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct Process *current;
  int sc;
  
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (is_allowed(vnode, R_OK) != 0) {
    return -EACCES;
  }

  vnode_lock(vnode);

  if (S_ISCHR(vnode->mode)) {
    sc = vfs_isatty (vnode);    
  } else {
  	sc = 0;
  }
  
  vnode_unlock(vnode);

  return sc;
}


/* @brief   Set attributes of terminal device
 *
 */
int ioctl_tcsetattr(int fd, struct termios *_termios)
{
  return -ENOSYS;
}


/* @brief   Get attributes of terminal device
 *
 */
int ioctl_tcgetattr(int fd, struct termios *_termios)
{
  return -ENOSYS;
}


/*
 *
 */
int ioctl_tiocsctty(int fd, int arg)
{
  struct VNode *vnode;
  struct Process *current;
    
  Info("ioctl_tiocsctty(fd:%d, arg:%d)", fd, arg);
  
  current = get_current_process();

  vnode = get_fd_vnode(current, fd);    // FIXME: does this increment ref count ?

  if (vnode == NULL) {
    Info("ioctl_tiocsctty -EINVAL");
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    return -EINVAL;
  }

  if (current->fproc->controlling_tty != NULL) {
    vnode_put(current->fproc->controlling_tty);  
  }  
  
  current->fproc->controlling_tty = vnode;
  vnode->tty_pgrp = get_current_pid();
  vnode_inc_ref(vnode);
  vnode_put(vnode);

  return 0;
}


/*
 *
 */
int ioctl_tiocnotty(int fd)
{
  struct VNode *vnode;
  struct Process *current;
  
  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    return -EINVAL;
  }
        
  if (vnode->tty_pgrp != current->pgrp) {
    return -EPERM;
  }  

  if (current->session_leader == true) {
    // Send SIGCONT and SIGHUP to foreground process group
    sys_kill(-vnode->tty_pgrp, SIGCONT);
    sys_kill(-vnode->tty_pgrp, SIGHUP);
  }
  
  vnode->tty_pgrp = -1;  
  vnode_put(vnode);

  return 0;
}
 
 
/* @brief   Set the file handle to be used to receive kernel debug logs
 *
 */
int ioctl_setsyslog(int fd)
{
  // TODO:  set syslog file
  return -ENOSYS;
}





