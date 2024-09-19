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
 * Need to allow readmsg/writemsg to read/write from the client's address space for certain
 * commands such as read, write, readdir and sendmsg.
 *
 * Note: There is no use of vnode->busy.  Instead we use vnode->reader_cnt and vnode->writer_cnt
 * All other commands are assumed to be going to the command type queue.
 *
 * This code limits the sending of 1 write, 1 read and 1 command at a time.
 *
 * TODO: Need to handle non-blocking reads and writes.  Add field to fsreq to indicate non-blocking?
 */
ssize_t read_from_char(struct VNode *vnode, void *dst, size_t sz)
{
  uint8_t buf[512];
  ssize_t xfered = 0;
  size_t xfer = 0;
  struct Process *current;
  
  Info("read_from_char(dst:%08x, sz:%d", (uint32_t)dst, sz);
  
  current = get_current_process();

//  if (vnode->isatty == true && vnode->tty_pgrp != current-pgrp) {
    // do_signal_thread(current, SIG);
//    return -EPERM;
//  }
    
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

//  Info("** read_from_char(sz:%d) xfered = %d", sz, xfered);

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

//  Info("write_to_char(src:%08x, sz:%d)", (uint32_t)src, sz);

  current = get_current_process();

//  if (vnode->isatty == true && vnode->tty_pgrp != current-pgrp) {
    // do_signal_thread(current, SIG);
//    return -EPERM;
//  }

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

//  Info("** write_to_char(sz:%d) xfered = %d", sz, xfered);

  return xfered;
}


/* @brief   Indicate if a file handle points to a TTY
 *
 * TODO: Remove CMD_ISATTY. Replace with mount() setting vnode->isatty = true
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


/* @brief   Make this the controlling terminal of the calling process.
 *
 * TODO: Check if tty
 */
int ioctl_tiocsctty(int fd, int arg)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
//  pid_t pgid; = (pid_t)arg;   // arg unused
  
  Info("ioctl_tiocsctty");
  
  current = get_current_process();

  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    Info("vnode not found");
    return -EINVAL;
  }

  if (S_ISCHR(vnode->mode) == 0) {
    Info("vnode not a character device");
    vnode_put(vnode);
    return -EINVAL;
  }

  session = get_session(current->sid);

  if (session == NULL) {
    Error("ioctl_tiocsctty - no session");
    vnode_put(vnode);
    return -EPERM;
  }

  if (vnode->tty_sid != INVALID_PID) {
    Error("ioctl_tiocsctty - tty already has session");
    vnode_put(vnode);
    return -EPERM;
  }

  KASSERT(session->sid != INVALID_PID);

  vnode->tty_sid = session->sid;
  session->controlling_tty = vnode;
  session->foreground_pgrp = current->pgid;
  vnode_inc_ref(vnode);
  
  vnode_put(vnode);

  return 0;
}


/* @brief   Give up a controlling terminal of the current session.
 *
 * NOTE: We give up the controlling terminal of the current session. This may be
 * different to POSIX behaviour which may only give it up for the calling process.
 *
 * TODO: Check if tty
 */
int ioctl_tiocnotty(int fd)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;

  Info("ioctl_tiocnotty");
  
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
 * TODO: Check if tty
 */
int ioctl_tiocgsid(int fd, pid_t *_sid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t sid;
  
  Info("ioctl_tiocgsid");

  
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
 * TODO: Check if tty
 */
int ioctl_tiocgpgrp(int fd, pid_t *_pgid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t pgid;

  Info("ioctl_tiocgpgrp");
  
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
 * TODO: Check if tty
 */ 
int ioctl_tiocspgrp(int fd, pid_t *_pgid)
{
  struct VNode *vnode;
  struct Process *current;
  struct Session *session;
  pid_t pgid;
  

  Info("ioctl_tiocspgrp");
  
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
 */
int ioctl_setsyslog(int fd)
{
  return -ENOTSUP;
}



