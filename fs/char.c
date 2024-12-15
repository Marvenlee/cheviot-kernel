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
 * Need to allow readmsg/writemsg to read/write from the client's address space for certain
 * commands such as read, write, readdir and sendmsg.
 *
 * This code limits the sending of 1 write and 1 read at a time.
 *
 * TODO: Need to handle non-blocking reads and writes.  Add field to iorequest to indicate non-blocking?
 * TODO: May want to wake up other tasks if reader_cnt is 0 on interruption
 *
 */
ssize_t read_from_char(struct VNode *vnode, void *dst, size_t sz)
{
  ssize_t xfered = 0;
  int sc;

  if ((sc = tty_fg_pgrp_check(vnode)) != 0) {
    return sc;
  }
  
  while (vnode->char_read_busy == true) {
    if (TaskSleepInterruptible(&vnode->rendez, NULL, INTRF_ALL) != 0) {
      return -EINTR;
    }    
  }

  vnode->char_read_busy = true;
    
  if (sz > 0) {
    xfered = vfs_read(vnode, IPCOPY, dst, sz, NULL);     
  }
  
  vnode->char_read_busy = false;
  TaskWakeupAll(&vnode->rendez);

  return xfered;
}


/* @brief   Write data to a character device
 *
 * TODO: Need to handle non-blocking reads and writes.  Add field to iorequest to indicate non-blocking?
 * TODO: May want to wake up other tasks if writer_cnt is 0 on interruption
 */
ssize_t write_to_char(struct VNode *vnode, void *src, size_t sz)
{
  size_t remaining;
  ssize_t xfered = 0;
  ssize_t total_xfered = 0;  
  int sc;
  
  if ((sc = tty_fg_pgrp_check(vnode)) != 0) {
    return sc;
  }
  
  while (vnode->char_write_busy == true) {
    if (TaskSleepInterruptible(&vnode->rendez, NULL, INTRF_ALL) != 0) {
      return -EINTR;
    }    
  }

  vnode->char_write_busy = true;
  remaining = sz;
    
  while(remaining > 0) {
    xfered = vfs_write(vnode, IPCOPY, src, remaining, NULL);    

    if (xfered <= 0) {
      break;  
    }
    
    remaining -= xfered;
    total_xfered += xfered;
    src += xfered;
  }

  vnode->char_write_busy = false;
  TaskWakeupAll(&vnode->rendez);    

  if (total_xfered == 0) {
    return xfered;
  }

  return total_xfered;
}


/* @brief   Indicate if a file handle points to a TTY
 *
 * TODO: Remove CMD_ISATTY. Replace with flag in mount() setting vnode->isatty = true
 * Useful also for checking if pipes are TTYs (these don't have user-mode driver) and
 * for the ioctls for controlling TTYs.
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


  if (check_access(vnode, filp, R_OK) != 0) {
    return -EACCES;
  }

  rwlock(&vnode->lock, LK_SHARED);

  if (S_ISCHR(vnode->mode)) {
    sc = vfs_isatty(vnode);    
  } else {
  	sc = 0;
  }
  
  rwlock(&vnode->lock, LK_RELEASE);

  return sc;
}


/* @brief   Check if we are a TTT and if so if we are in the foreground process group
 *
 */
int tty_fg_pgrp_check(struct VNode *vnode)
{
#if 0
  if (vnode->isatty == true && vnode->tty_pgrp != current-pgrp) {
    do_signal_thread(current, SIG);
    return -EPERM;
  }
#else
  return 0;
#endif
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


/* @brief   Make this the controlling terminal of the calling process.
 *
 * TODO: Check if the vnode is a tty
 */
int ioctl_tiocsctty(int fd, int arg)
{  
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  // pid_t pgid; = (pid_t)arg;   // arg unused
  
  current = get_current_process();

  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    vnode_put(vnode);
    return -EINVAL;
  }

  session = get_session(current->sid);

  if (session == NULL) {
    vnode_put(vnode);
    return -EPERM;
  }

  if (vnode->tty_sid != INVALID_PID) {
    vnode_put(vnode);
    return -EPERM;
  }

  KASSERT(session->sid != INVALID_PID);

  if (session->controlling_tty != NULL) {
    vnode_put(session->controlling_tty);
    session->controlling_tty = NULL;
  }

  vnode->tty_sid = session->sid;
  session->controlling_tty = vnode;
  session->foreground_pgrp = current->pgid;
  vnode_add_reference(vnode);
  return 0;
}


/* @brief   Give up a controlling terminal of the current session.
 *
 * NOTE: We give up the controlling terminal of the current session. This may be
 * different to POSIX behaviour which may only give it up for the calling process.
 *
 * TODO: Check if the vnode is a tty
 */
int ioctl_tiocnotty(int fd)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    vnode_put(vnode);
    return -EINVAL;
  }

  session = get_session(current->sid);

  if (session == NULL) {
    vnode_put(vnode);
    return -EPERM;
  }

  if (current->pid == session->sid) {
    // TODO: replace with kill_foreground_pgrp(session)
    sys_kill(-session->foreground_pgrp, SIGCONT);
    sys_kill(-session->foreground_pgrp, SIGHUP);
  }
  
  if (vnode->tty_sid == session->sid) {
    vnode->tty_sid = INVALID_PID;
    session->controlling_tty = NULL;
    // FIXME: Do we need a vnode_dec_ref() ?
    vnode_put(vnode);          
  }
  
  return 0;
}
 

/* @brief   Get the session ID of the terminal
 *
 * TODO: Check if the vnode is a tty
 */
int ioctl_tiocgsid(int fd, pid_t *_sid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t sid;
  
  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    vnode_put(vnode);
    return -EINVAL;
  }

  session = get_session(vnode->tty_sid);

  if (session == NULL) {
    vnode_put(vnode);
    return -EPERM;
  }

  if (current->sid != session->sid) {
    vnode_put(vnode);
    return -EPERM;  
  }

  sid = session->sid;
  vnode_put(vnode);
  
  if (CopyOut(_sid, &sid, sizeof _sid) != 0) {
    return -EFAULT;
  }
  
  return 0;
}


/* @brief   Get the foreground process group ID of the terminal.
 *
 * TODO: Check if the vnode is a tty
 */
int ioctl_tiocgpgrp(int fd, pid_t *_pgid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t pgid;

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    return -EINVAL;
  }

  session = get_session(vnode->tty_sid);

  if (session == NULL) {
    vnode_put(vnode);
    return -EPERM;
  }

  if (current->sid != session->sid) {
    return -EPERM;
  }

  pgid = session->foreground_pgrp;

  vnode_put(vnode);
          
  if (CopyOut(_pgid, &pgid, sizeof *_pgid) != 0) {
    return -EFAULT;
  }
  
  return 0;
}


/* @brief   Set the foreground process group ID of the terminal.
 *
 * TODO: Check if the vnode is a tty
 */ 
int ioctl_tiocspgrp(int fd, pid_t *_pgid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t pgid;
  
  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    return -EINVAL;
  }

  if (CopyIn(&pgid, _pgid, sizeof pgid) != 0) {
    return -EFAULT;
  }

  session = get_session(vnode->tty_sid);

  if (session == NULL) {
    vnode_put(vnode);
    return -EPERM;
  }

  if (current->sid != session->sid) {
    vnode_put(vnode);
    return -EPERM;
  }

  session->foreground_pgrp = pgid;
  vnode_put(vnode);

  return 0;
}

 
/* @brief   Set the target file descriptor where debug logs are sent
 *
 * TODO: Work out a way to send kernel logs to a syslog user-mode server.
 * Either FD is a file or the server's message port.
 */
int ioctl_setsyslog(int fd)
{
  return -ENOTSUP;
}



