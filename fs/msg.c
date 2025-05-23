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
 * Message passing system calls that the VFS uses to communicate with servers that
 * implement filesystem handlers, block and character device drivers.
 *
 * Filesystem requests are converted to multi-part IOV messages.  The server
 * typically uses kqueue's kevent() to wait for a message to arrive. The server
 * then uses ReceiveMsg to receive the header of a message.  The remainder of
 * the message can be read or modified using ReadMsg and WriteMsg. The
 * server indicates it is finished with the message by calling ReplyMsg.
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <kernel/kqueue.h>
#include <sys/syscalls.h>
#include <sys/iorequest.h>


/* @brief   Get a message from a mount's message port
 *
 * @param   fd, file descriptor of mount created by sys_mount()
 * @param   msgidp, user address to store the message's msgid
 * @param   _req, address of buffer to read the iorequest message header into
 * @param   req_sz, size of buffer to read iorequest message header into
 * @return  size of read iorequest header or negative errno on error.
 *
 * This is a non-blocking function.  Use in conjunction with kevent() in order
 * to wait for messages to arrive.
 *
 * TODO: Revert msgid to sender's process ID + thread ID, change type to endpoint_t.
 * FIXME: Check return values of ipcopy and copyout
 * TODO: Check range is user space for source and dest
 */
int sys_getmsg(int fd, msgid_t *_msgid, iorequest_t *_req, size_t req_sz)
{
  struct SuperBlock *sb;  
  struct MsgPort *msgport;
  struct Msg *msg;
  msgid_t msgid;
  struct Process *current_proc;

  if (req_sz < sizeof(iorequest_t) || _msgid == NULL || _req == NULL) {
    return -EINVAL;
  }
  
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;
  
  msg = kpeekmsg(msgport);
  
  if (msg == NULL) {
    knote_dequeue(&msgport->knote_list, EVFILT_MSGPORT);
    return 0;
  }
  
  if (msg->req->cmd != CMD_ABORT) {
    msg = kgetmsg(msgport);
    
    if (msg == NULL) {
      knote_dequeue(&msgport->knote_list, EVFILT_MSGPORT);
      return 0;
    }
  } else {
    kremovemsg(msgport, msg);  
  }

  msgid = msg->msgid;

  CopyOut (_msgid, &msgid, sizeof (msgid_t));
  CopyOut (_req, msg->req, sizeof (iorequest_t));

  return sizeof(iorequest_t);
}


/* @brief   Reply to a message
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   status, error status to return to caller (0 on success or negative errno)
 * @param   rep, pointer to optional ioreply for commands that require it.
 * @param   rep_sz, size of ioreply_t
 * @return  0 on success, negative errno on error 
 *
 * FIXME: Check return values of ipcopy and copyout
 * TODO: Check range is user space for source and dest
 */
int sys_replymsg(int fd, msgid_t msgid, int status, ioreply_t *rep, size_t rep_sz)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;

  if (rep != NULL && rep_sz != sizeof(ioreply_t)) {
    return -EINVAL;
  }
  
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);

  if (sb == NULL) {
    return -EINVAL;
  }
  
  if ((msg = msgid_to_msg(&sb->msgport, msgid)) == NULL) {
    return -EINVAL;
  }

  msg->reply_status = status;

  if (msg->reply != NULL && rep != NULL) {
    if(CopyIn (msg->reply, rep, sizeof (ioreply_t)) != 0) {
      msg->reply_status = -EFAULT;
    }    
  } else if (msg->reply != NULL && rep == NULL) {
    msg->reply_status = -EFAULT;
  }
     
  KASSERT(msg->reply_port != NULL);
  
	kreplymsg(msg);
 	 	
  return 0;
}


/* @brief   Read from a message
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   addr, address of buffer to read into
 * @param   buf_sz, size of buffer to read into
 * @param   offset, offset within the message to read
 * @return  number of bytes read on success, negative errno on error 
 *
 * FIXME: Check return values of ipcopy and copyout
 * TODO: Check range is user space for source and dest
 */
int sys_readmsg(int fd, msgid_t msgid, void *addr, size_t buf_sz, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;
  size_t nbytes_to_read;
  ssize_t nbytes_read;
  size_t buf_remaining;
  size_t iov_remaining;
  off_t iov_offset;
  int i;
  int sc;
          
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  if ((msg = msgid_to_msg(&sb->msgport, msgid)) == NULL) {
    return -EINVAL;
  }
    
  if (msg->siov_cnt == 0 || msg->siov_cnt >= IOV_MAX) {
    return -EINVAL;
  }
  
  if (seekiov(msg->siov_cnt, msg->siov, offset, &i, &iov_remaining, &iov_offset) != 0) {
    return -EINVAL;
  }

  nbytes_read = 0;
  buf_remaining = buf_sz;
    
  while (i < msg->siov_cnt && nbytes_read < buf_sz && buf_remaining > 0) {
    nbytes_to_read = (buf_remaining < iov_remaining) ? buf_remaining : iov_remaining;

    if (msg->ipc == IPCOPY) {
      sc = ipcopy(&current_proc->as, msg->src_as,
                  addr + nbytes_read,
                  msg->siov[i].addr + iov_offset,
                  nbytes_to_read);
    } else {
      sc = CopyOut(addr + nbytes_read,
                   msg->siov[i].addr + iov_offset,
                   nbytes_to_read);
    }

    if (sc != 0) {
      return -EFAULT;
    }

    nbytes_read += nbytes_to_read;
    buf_remaining = buf_sz - nbytes_read;
    iov_remaining -= nbytes_to_read;
    iov_offset += nbytes_to_read;
        
    if (iov_remaining == 0) {
      i++;
      iov_remaining = msg->siov[i].size;
      iov_offset = 0;
    }      
  }
    
  return nbytes_read;
}


/* @brief   Write to a message
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   addr, address of buffer to write from
 * @param   buf_sz, size of buffer to write from
 * @param   offset, offset within the message to write
 * @return  number of bytes written on success, negative errno on error 
 *
 * FIXME: Check return values of ipcopy and copyout
 * FIXME: Check range is user space for source and dest
 * FIXME: Update variables to size_t
 */
int sys_writemsg(int fd, msgid_t msgid, void *addr, size_t buf_sz, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;
  size_t nbytes_to_write;
  ssize_t nbytes_written;
  size_t buf_remaining;
  size_t iov_remaining;
  off_t iov_offset;
  int i;
  int sc;

  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  if ((msg = msgid_to_msg(&sb->msgport, msgid)) == NULL) {
    return -EINVAL;
  }

  if (msg->riov == NULL || msg->riov_cnt == 0 || msg->riov_cnt > IOV_MAX) {
    return -EINVAL;
  }
  
  if (seekiov(msg->riov_cnt, msg->riov, offset, &i, &iov_remaining, &iov_offset) != 0) {
    return -EINVAL;
  }

  nbytes_written = 0;
  buf_remaining = buf_sz;

  while (i < msg->riov_cnt && nbytes_written < buf_sz && buf_remaining > 0) {
    nbytes_to_write = (iov_remaining < buf_remaining) ? iov_remaining : buf_remaining;

    if (msg->ipc == IPCOPY) {
      sc = ipcopy(msg->src_as, &current_proc->as,
                  msg->riov[i].addr + iov_offset,
                  addr + nbytes_written,
                  nbytes_to_write);
    } else {      
      sc = CopyIn(msg->riov[i].addr + iov_offset,
                  addr + nbytes_written,
                  nbytes_to_write);
    }
           
    if (sc != 0) {
      return -EFAULT;
    }
    
    nbytes_written += nbytes_to_write;
    buf_remaining = buf_sz - nbytes_written;

    iov_remaining -= nbytes_to_write;
    iov_offset += nbytes_to_write;
        
    if (iov_remaining == 0) {
      i++;
      iov_remaining = msg->riov[i].size;
      iov_offset = 0;
    }
  }

  return nbytes_written;
}


/* @brief   Read from a message to a multi-part buffer
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   iov_cnt, number of io vectors
 * @param   _iov, array of io vectors defining buffer to read message data into
 * @param   offset, offset within the message to read
 * @return  number of bytes read on success, negative errno on error 
 */
int sys_readmsgiov(int fd, msgid_t msgid, int iov_cnt, msgiov_t *_iov, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;
  msgiov_t iov[IOV_MAX];
  int sc;
  ssize_t nbytes_read;
  size_t nbytes_to_read;
  int si;
  size_t siov_remaining;
  off_t siov_offset;
  int xi;
  size_t xiov_remaining;
  off_t xiov_offset;
  
  if (iov_cnt < 1 || iov_cnt > IOV_MAX) {
    return -EINVAL;
  }
  
  if (CopyIn(iov, _iov, sizeof(msgiov_t) * iov_cnt) != 0) {
    return -EFAULT;
  } 
      
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  if ((msg = msgid_to_msg(&sb->msgport, msgid)) == NULL) {
    return -EINVAL;
  }
  
  if (msg->siov_cnt == 0 || msg->siov_cnt > IOV_MAX) {
    return -EINVAL;
  }
    
  if (seekiov(msg->siov_cnt, msg->siov, offset, &si, &siov_remaining, &siov_offset) != 0) {
    return -EINVAL;
  }

  xi = 0;
  xiov_remaining = iov[0].size;
  xiov_offset = 0;
  nbytes_read = 0;
  
  while (si < msg->siov_cnt && xi < iov_cnt) {
    nbytes_to_read = (siov_remaining < xiov_remaining) ? siov_remaining : xiov_remaining;

    if (msg->ipc == IPCOPY) {
      sc = ipcopy(&current_proc->as, msg->src_as,
                  iov[xi].addr + xiov_offset,
                  msg->siov[si].addr + siov_offset,
                  nbytes_to_read);
    } else {
      sc = CopyOut(iov[xi].addr + xiov_offset,
                   msg->siov[si].addr + siov_offset,
                   nbytes_to_read);
    }

    if (sc != 0) {
      // FIXME: Return -EFAULT ?
      break;
    }
    
    nbytes_read += nbytes_to_read;      
    xiov_remaining -= nbytes_to_read;
    siov_remaining -= nbytes_to_read;
    xiov_offset += nbytes_to_read;
    siov_offset += nbytes_to_read;
    
    if (xiov_remaining == 0) {     
      xi++;
      xiov_remaining = iov[xi].size;
      xiov_offset = 0;
    }

    if (xiov_remaining == 0) {     
      si++;
      siov_remaining = msg->siov[si].size;
      siov_offset = 0;
    }
  }
    
  return nbytes_read;
}


/* @brief   Write to a message from a multi-part buffer
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   addr, address of buffer to write from
 * @param   buf_sz, size of buffer to write from
 * @param   offset, offset within the message to write
 * @return  number of bytes written on success, negative errno on error 
 */
int sys_writemsgiov(int fd, msgid_t msgid, int iov_cnt, msgiov_t *_iov, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;
  msgiov_t iov[IOV_MAX];
  int sc;
  ssize_t nbytes_written;
  size_t nbytes_to_write;
  int ri;
  size_t riov_remaining;
  off_t riov_offset;
  int xi;  
  size_t xiov_remaining;
  off_t xiov_offset;
    
  if (iov_cnt < 1 || iov_cnt > IOV_MAX) {
    return -EINVAL;
  }
  
  if (CopyIn(iov, _iov, sizeof(msgiov_t) * iov_cnt) != 0) {
    return -EFAULT;
  } 
      
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
    
  if ((msg = msgid_to_msg(&sb->msgport, msgid)) == NULL) {
    return -EINVAL;
  }
  
  if (msg->riov == NULL || msg->riov_cnt == 0) {
    return 0;
  }
  
  if (seekiov(msg->riov_cnt, msg->riov, offset, &ri, &riov_remaining, &riov_offset) != 0) {
    return -EINVAL;
  }

  xi = 0;
  xiov_remaining = iov[0].size;
  xiov_offset = 0;
  nbytes_written = 0;
  
  while (ri < msg->riov_cnt && xi < iov_cnt) {
    nbytes_to_write = (riov_remaining < xiov_remaining) ? riov_remaining : xiov_remaining;

    if (msg->ipc == IPCOPY) {
      sc = ipcopy(msg->src_as, &current_proc->as,
                  msg->riov[ri].addr + riov_offset,
                  iov[xi].addr + xiov_offset,
                  nbytes_to_write);
    } else {      
      sc = CopyIn(msg->riov[ri].addr + riov_offset,
                  iov[xi].addr + xiov_offset,
                  nbytes_to_write);
    }
       
    if (sc != 0) {
      // FIXME: return -efault
      break;
    }
    
    nbytes_written += nbytes_to_write;      
    riov_remaining -= nbytes_to_write;
    xiov_remaining -= nbytes_to_write;
    riov_offset += nbytes_to_write;
    xiov_offset += nbytes_to_write;

    if (riov_remaining == 0) {     
      ri++;
      riov_remaining = msg->riov[ri].size;
      riov_offset = 0;
    }

    if (xiov_remaining == 0) {
      xi++;
      xiov_remaining = msg->riov[xi].size;
      xiov_offset = 0;
    }
  }

  return nbytes_written;
}


/* @brief   Synchronously sendd a message to a server
 *
 * @param   fd, file descriptor of opened connection to server
 * @param   subclass, subclass of the CMD_SENDMSG command
 * @param   siov_cnt, count of iov vectors of data to send to server
 * @param   _siov, array of iov vectors of data to send to server
 * @param   riov_cnt, count of iov vectors of buffers to receive data from server
 * @param   _riov, array of iov vectors of buffers to receive data from server
 * @return  0 or positive value on success, negative errno on failure.
 *
 * This is intended for custom RPC messages that don't follow the predefined
 * filesystem commands.  The kernel will send an iorequest to the server with:
 *
 * req.cmd = CMD_SENDMSG
 * req.u.sendmsg.subclass = subclass
 * req.u.sendmsg.ssize = total size of sent siov buffers
 * req.u.sendmsg.rsize = total size of response riov buffers
 *
 * A connection to a server must be established with the open() system call.
 *
 * TODO: Check bounds of IOVs. 
 */
int sys_sendio(int fd, int subclass, int siov_cnt, msgiov_t *_siov, int riov_cnt, msgiov_t *_riov)
{
  struct Process *current;
  struct Filp *filp;
  struct VNode *vnode;
  msgiov_t siov[IOV_MAX];
  msgiov_t riov[IOV_MAX];
  size_t sbuf_total_sz = 0;
  size_t rbuf_total_sz = 0;
  int sc;
    
  if (siov_cnt < 1 || siov_cnt > IOV_MAX || riov_cnt < 0 || riov_cnt > IOV_MAX) {
    return -EINVAL;
  }

  if (CopyIn(&siov[0], _siov, sizeof(msgiov_t) * siov_cnt) != 0) {
    return -EFAULT;
  }

  if (riov_cnt > 0) {
    if (CopyIn(riov, _riov, sizeof(msgiov_t) * riov_cnt) != 0) {
      return -EFAULT;
    }
  }

  for (int t=0; t< siov_cnt; t++) {
    sbuf_total_sz += siov[t].size;
  }

  for (int t=0; t< siov_cnt; t++) {
    rbuf_total_sz += riov[t].size;
  }
  
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EBADF;
  }

  if (check_access(vnode, filp, X_OK) != 0) {
    return -EACCES;
  }

  rwlock(&vnode->lock, LK_SHARED);

  sc = vfs_sendmsg(vnode, subclass, siov_cnt, siov, riov_cnt, riov, sbuf_total_sz, rbuf_total_sz);

  rwlock(&vnode->lock, LK_RELEASE);
  
  return sc;
}


/* @brief   Asynchronously send a message to a server.
 *
 */
int sys_beginio(int fd, int subclass, int siov_cnt, msgiov_t *_siov, int riov_cnt, msgiov_t *_riov)
{
  return -ENOSYS;
}


/* @brief   Allocate buffers to handle asynchronous messages
 *
 */
int sys_alloc_asyncio(int n)
{
  return -ENOSYS;
}


/* @brief   Free buffers used to handle asynchronous messages
 *
 */
int sys_free_asyncio(int n)
{
  return -ENOSYS;
}


/* @brief   Send a message to a message port and wait for a reply.
 *
 * TODO:  Need timeout and abort mechanisms into IPC in case server doesn't respond or terminates.
 * Also timeout works for when a server tries to create a second mount in its own mount path.
 *
 * Checks if message is on pending queue (not yet received by server)
 * If so, silently remove message, set msg._reply_status to -EINTR and return. 
 *    
 * TODO: Check signals, we may want to kill the current_proc process immediately. 
 */
int ksendmsg(struct MsgPort *msgport, int ipc, iorequest_t *req, ioreply_t *reply,
             int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov)
{
  struct Process *current_proc;
  struct Thread *current_thread;
  struct Msg msg;
	int sc;
	
	KASSERT(req != NULL);
	
  current_proc = get_current_process();
  current_thread = get_current_thread();

  current_thread->msg = &msg;
  msg.msgid = current_thread->tid;
  msg.reply_port = &current_thread->reply_port;  
  msg.siov_cnt = siov_cnt;
  msg.siov = siov;
  msg.riov_cnt = riov_cnt;
  msg.riov = riov;  
  msg.reply_status = 0;
  msg.ipc = ipc;  
  msg.src_as = (ipc == IPCOPY) ? &current_proc->as : NULL;
  msg.req = req;
  msg.reply = reply;
    
  kputmsg(msgport, &msg);   
  
  while ((kwaitport(&current_thread->reply_port, NULL)) != 0) {   
    sc = kabortmsg(msgport, &msg);

    if (sc != 0) {
      return sc;
    }
  }

  kgetmsg(&current_thread->reply_port);
  current_thread->msg = NULL;

  return msg.reply_status;
}


/* @brief   Abort a message that has been sent to a message port
 *
 * TODO: We also need to handle the case of the message port closing due to either closing
 * of the file handle or the server process terminating or the client being killed and
 * and so messages need an immediate disconnect.
 *
 * We may want to handle aborts with a timeout.
 * We may want to ignore secondary aborts due to signals until a timeout expires.
 * e.g. 2 successive SIGTERMs will send one MSG_ABORT to server, then the second one will
 * abort the whole thing and not wait for the driver to complete it's abort message handling.
 */
int kabortmsg(struct MsgPort *msgport, struct Msg *msg)
{
  iorequest_t *req;
  struct Thread *current_thread = get_current_thread();
      
  if (msg->msgid != INVALID_PID) {
    // message has been received and is assigned a msgid, send it again with CMD_ABORT
    // IOVs will remain the same so readmsg, writemsg and replymsg will be ok.        
    req = msg->req;
    
    if (req->cmd == CMD_ABORT) {
      // An abort is already in progress, invalidate the message so that the server
      // cannot perform any readmsg,writemsg or replymsg on this message.
      msg->port = NULL;
      msg->msgid = INVALID_PID;
      msg->reply_status = -EINTR;
      current_thread->msg = NULL;
      return -EINTR;
    }
    
    req->cmd = CMD_ABORT;
        
    LIST_ADD_TAIL(&msgport->pending_msg_list, msg, link);
    knote(&msgport->knote_list, NOTE_MSG);
    
    return 0;
    
  } else if (msg->port == msgport) {
    // message has not been received, but is still on message list of server, remove it 
    kremovemsg(msgport, msg);

    msg->port = NULL;
    msg->msgid = INVALID_PID;
    msg->reply_status = -EINTR;
    current_thread->msg = NULL;
    return -EINTR;

  } else {
    // message has replied
    return 0;
  }
}


/* @brief   Send a message to a message port but do not wait
 *
 * The calling function must already allocate and set the msgid of the message
 * msg already has the msg.msgid set.
 */
int kputmsg(struct MsgPort *msgport, struct Msg *msg)
{
  msg->port = msgport;
  LIST_ADD_TAIL(&msgport->pending_msg_list, msg, link);

  knote(&msgport->knote_list, NOTE_MSG);   // NOTE_MSG_RECEIVED 

  return 0;
}


/* @brief   Reply to a kernel message
 *
 */
int kreplymsg(struct Msg *msg)
{
  struct MsgPort *reply_port;
  
  KASSERT (msg != NULL);  
  KASSERT (msg->reply_port != NULL);
  
  msg->port = msg->reply_port;
  reply_port = msg->reply_port;
  LIST_ADD_TAIL(&reply_port->pending_msg_list, msg, link);
  TaskWakeup(&reply_port->rendez);
  return 0;  
}


/* @brief   Get a kernel message from a message port
 */
struct Msg *kgetmsg(struct MsgPort *msgport)
{
  struct Msg *msg;
    
  msg = LIST_HEAD(&msgport->pending_msg_list);
  
  if (msg) {
    LIST_REM_HEAD(&msgport->pending_msg_list, link);
  }
  
  return msg;
}


/*
 *
 */
struct Msg *kpeekmsg(struct MsgPort *msgport)
{   
  return LIST_HEAD(&msgport->pending_msg_list);
}


/*
 *
 */
void kremovemsg(struct MsgPort *msgport, struct Msg *msg)
{   
  LIST_REM_ENTRY(&msgport->pending_msg_list, msg, link);
}


/* @brief   Wait for a message port to receive a message
 *
 * @param   msgport, message port to wait on
 * @param   timeout, duration to wait for a message
 * @return  0 on success,
 *          -ETIMEDOUT on timeout
 *          negative errno on other failure
 *
 * TODO: Allow kwaitport to be interrupted by signals.
 */
int kwaitport(struct MsgPort *msgport, struct timespec *timeout)
{
  int sc;
  
  if (LIST_HEAD(&msgport->pending_msg_list) == NULL) {
    if ((sc = TaskSleepInterruptible(&msgport->rendez, timeout, INTRF_NONE)) != 0) {
      if (LIST_HEAD(&msgport->pending_msg_list) == NULL) {
        return sc;
      }
    }
  }

  return 0;  
}


/* @brief   Initialize a message port
 *
 * @param   msgport, message port to initialize
 * @return  0 on success, negative errno on error
 */
int init_msgport(struct MsgPort *msgport)
{
  LIST_INIT(&msgport->pending_msg_list);
  LIST_INIT(&msgport->knote_list);
  InitRendez(&msgport->rendez);
  msgport->context = NULL;
  return 0;
}


/* @brief   Cleanup a message port
 * 
 * @param   msgport, message port to cleanup
 * @return  0 on success, negative errno on error
 * 
 * TODO: Reply to any pending or in progress messages
 */
int fini_msgport(struct MsgPort *msgport)
{
  return 0;
} 


/* @brief   Get a pointer to message from the message ID.
 *
 * @param   msgport, pointer to server message port
 * @param   msgid, message ID of the message to lookup.
 * @return  Pointer to message structure, or NULL on failure
 *
 */
struct Msg *msgid_to_msg(struct MsgPort *msgport, msgid_t msgid)
{
  struct Thread *thread;
  struct Msg *msg;
  
  KASSERT(msgport != NULL);
  
  thread = get_thread(msgid);
  
  if (thread == NULL || thread->msg == NULL) {
    return NULL;
  }  

  msg = thread->msg;
  
  if (msg->port != msgport) {
    return NULL;
  }

  return msg;
}


/* @brief   Seek to a position within a multi-part message
 *
 */
int seekiov(int iov_cnt, msgiov_t *iov, off_t offset, int *ret_i, size_t *ret_remaining, off_t *ret_offset)
{
  off_t base_offset = 0;
  int i;
  size_t iov_remaining = 0;
  off_t iov_offset = 0;
  
  if (offset < 0) {
    return -EINVAL;
  }
  
  KASSERT(iov_cnt > 0);

  for (i = 0; i < iov_cnt; i++) {
    if (offset >= base_offset && offset < base_offset + iov[i].size) {
      iov_remaining = base_offset + iov[i].size - offset;
      iov_offset = offset - base_offset;
      break;
    }

    base_offset += iov[i].size;
  }

  if (i >= iov_cnt) {
    return -EINVAL;
  }

  *ret_i = i;
  *ret_remaining = iov_remaining;
  *ret_offset = iov_offset;
  
  return 0;
}

