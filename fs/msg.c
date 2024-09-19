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

/*
 * Message passing system calls that the VFS uses to communicate with servers that
 * implement filesystem handlers, block and character device drivers.
 *
 * Filesystem requests are converted to multi-part IOV messages.  The server
 * typically uses kqueue's kevent() to wait for a message to arrive. The server
 * then uses ReceiveMsg to receive the header of a message.  The remainder of
 * the message can be read or modified using ReadMsg and WriteMsg. The
 * server indicates it is finished with the message by calling ReplyMsg.
 */

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <kernel/kqueue.h>
#include <sys/syscalls.h>


/* @brief   Get a message from a mount's message port
 *
 * @param   fd, file descriptor of mount created by sys_mount()
 * @param   msgidp, user address to store the message's msgid
 * @param   addr, address of buffer to read the initial part of message into
 * @param   buf_sz, size of buffer to read into
 * @return  number of bytes read or negative errno on error.
 *
 * This is a non-blocking function.  Use in conjunction with kevent() in order
 * to wait for messages to arrive.
 *
 * TODO: Revert msgid to sender's process ID + thread ID, change type to endpoint_t.
 * 
 */
int sys_getmsg(int fd, msgid_t *_msgid, void *addr, size_t buf_sz)
{
  struct SuperBlock *sb;  
  struct MsgPort *msgport;
  struct Msg *msg;
  msgid_t msgid;
  off_t offset;
  int nbytes_read;
  int nbytes_to_xfer;
  int remaining;
  int i;
  struct Process *current_proc;

  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;
  
  msg = kpeekmsg(msgport);
  
  if (msg == NULL) {
    return 0;
  }
  
  if (msg->msgid != -1) {
    // This message is a CMD_ABORT message, we have resent the same message with the cmd field
    // changed and has the same msgid.
    msgid = msg->msgid;
    kremovemsg(msgport, msg);
  
  } else {
    // Check if we have a backlog slot free?
    // Allocate msgid slot for backlog (sb->msgport_backlog_table);
    msgid = alloc_msgid(&sb->msgbacklog);
    
    if (msgid < 0) {
    	return -EAGAIN;
    }
    
    msg = kgetmsg(msgport);

    if (msg == NULL) {
      free_msgid(&sb->msgbacklog, msgid);
      return 0;
    }  
    
    assign_msgid(&sb->msgbacklog, msgid, msg);
  }
  
  CopyOut (_msgid, &msgid, sizeof msgid);

  nbytes_read = 0;
  
  if (msg->siov_cnt > 0) {  
    i = 0;
    offset = 0;

    while (i < msg->siov_cnt && nbytes_read < buf_sz) {
      remaining = buf_sz - offset;
      nbytes_to_xfer = (msg->siov[i].size < remaining) ? msg->siov[i].size : remaining;

      CopyOut(addr + nbytes_read, msg->siov[i].addr, nbytes_to_xfer);
      nbytes_read += nbytes_to_xfer;
      offset += nbytes_to_xfer;
      i++;
    }
  }
  
  return nbytes_read;
}


/* @brief   Reply to a message
 *
 * @param   fd, file descriptor of mounted file system created with sys_createmsgport()
 * @param   msgid, unique message identifier returned by sys_getmsg()
 * @param   status, error status to return to caller (0 on success or negative errno)
 * @param   addr, address of buffer to write from
 * @param   buf_sz, size of buffer to write from
 * @return  0 on success, negative errno on error 
 */
int sys_replymsg(int fd, msgid_t msgid, int status, void *addr, size_t buf_sz)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_write;
  int nbytes_written;
  int remaining;
  int iov_remaining;
  int i;
  int sc;
    
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);

  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;

  if ((msg = msgid_to_msg(&sb->msgbacklog, msgid)) == NULL) {
    return -EINVAL;
  }

  msg->reply_status = status;

	// FIXME: We may not have an riov (riov) can be null

  if (msg->riov_cnt > 0) {
    iov_remaining = msg->riov[0].size;
    i = 0;
    nbytes_written = 0;

    while (i < msg->riov_cnt && nbytes_written < buf_sz) {
      remaining = buf_sz - nbytes_written;
      nbytes_to_write = (msg->riov[i].size < remaining) ? msg->riov[i].size : remaining;

      sc = CopyIn(msg->riov[i].addr + msg->riov[i].size - iov_remaining,
             addr + nbytes_written, nbytes_to_write);
       
      if (sc != 0) {
        break;
      }
          
      nbytes_written += nbytes_to_write;
      i++;
      iov_remaining = msg->riov[i].size;
    }
  }
  
  KASSERT(msg->reply_port != NULL);
  
	kreplymsg(msg);
 	 	
	free_msgid(&sb->msgbacklog, msgid);
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
 */
int sys_readmsg(int fd, msgid_t msgid, void *addr, size_t buf_sz, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_read;
  int nbytes_read;
  int buf_remaining;
  int iov_remaining;
  int i;
    
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;

  if ((msg = msgid_to_msg(&sb->msgbacklog, msgid)) == NULL) {
    return -EINVAL;
  }
  
  nbytes_read = 0;
  
  if (msg->siov_cnt > 0) {
    if (seekiov(msg->siov_cnt, msg->siov, offset, &i, &iov_remaining) != 0) {
      return -EINVAL;
    }

    while (i < msg->siov_cnt && nbytes_read < buf_sz) {
      buf_remaining = buf_sz - nbytes_read;
      nbytes_to_read = (buf_remaining < iov_remaining) ? buf_remaining : iov_remaining;

      CopyOut(addr + nbytes_read,
              msg->siov[i].addr + msg->siov[i].size - iov_remaining,
              nbytes_to_read);

      nbytes_read += nbytes_to_read;
      i++;
      iov_remaining = msg->siov[i].size;
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
 */
int sys_writemsg(int fd, msgid_t msgid, void *addr, size_t buf_sz, off_t offset)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_write;
  int nbytes_written;
  int buf_remaining;
  int iov_remaining;
  int i;
  int sc;

  
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;
  
  if ((msg = msgid_to_msg(&sb->msgbacklog, msgid)) == NULL) {
    return -EINVAL;
  }
  
	// FIXME: Check we have a valid riov (it can be null or zero length
  nbytes_written = 0;

  if (msg->riov_cnt > 0) {
    if (seekiov(msg->riov_cnt, msg->riov, offset, &i, &iov_remaining) != 0) {
      return -EINVAL;
    }

    while (i < msg->riov_cnt && nbytes_written < buf_sz) {
      buf_remaining = buf_sz - nbytes_written;
      nbytes_to_write = (iov_remaining < buf_remaining) ? iov_remaining : buf_remaining;

      sc = CopyIn(msg->riov[i].addr + msg->riov[i].size - iov_remaining,
             addr + nbytes_written, nbytes_to_write);
       
      if (sc != 0) {
        break;
      }
      
      nbytes_written += nbytes_to_write;
      i++;
      iov_remaining = msg->riov[i].size;
    }
  }
  
  return nbytes_written;
}


/* @brief   Blocking send and receive message to a RPC service
 *
 * This is intended for custom RPC messages that don't follow the predefined
 * filesystem commands.  The kernel will prefix messages with a fsreq IOV with
 * cmd=CMD_SENDREC before being sent to the server.
 *
 * TODO: This is currently implemented as a quick and dirty hack.  We only
 * support short messages as we need to copy them into the kernel before sending
 * to the server.  The same applies to replies.
 *
 * When we implement address-space to address-space direct copying we can
 * remove the copying into/out of the kernel and handle any size messages upto
 * the limits of the address space.
 *
 * This also applies to read() and write() calls for block and character devices
 * that also copy data into temporary kernel buffers before sending them onto
 * the server.
 *
 * Finally we may also want to tag certain IOV vectors so that they can be
 * marked eligible for page-swapping with the server for zero-copy IPC.  
 */
ssize_t sys_sendmsg(int fd, int siov_cnt, msgiov_t *_siov, int riov_cnt, msgiov_t *_riov)
{
  struct Process *current;
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct fsreq req;
  msgiov_t siov[8];
  msgiov_t riov[8];
  uint8_t sbuf[512];
  uint8_t rbuf[512];
  size_t sbuf_total_sz;
  size_t rbuf_total_sz;
  msgiov_t msgsiov[2];
  msgiov_t msgriov[1];  
  ssize_t xfered;
  ssize_t nbytes_response;
  int nbytes_to_read;
  int nbytes_read;
  int nbytes_to_write;
  int nbytes_written;
  int buf_remaining;
  int iov_remaining;
  int i;
  
  if (siov_cnt < 1 || siov_cnt > 8 || riov_cnt < 0 || riov_cnt > 8) {
    return -EINVAL;
  }

  if (CopyIn(siov, _siov, sizeof(msgiov_t) * siov_cnt) != 0) {
    return -EFAULT;
  }

  if (riov_cnt > 0) {
    if (CopyIn(riov, _riov, sizeof(msgiov_t) * riov_cnt) != 0) {
      return -EFAULT;
    }
  }

  sbuf_total_sz = 0;
  for (int t=0; t<siov_cnt; t++) {
    sbuf_total_sz += siov[t].size;
  }

  rbuf_total_sz = 0;
  for (int t=0; t<riov_cnt; t++) {
    rbuf_total_sz += riov[t].size;
  }

  if (sbuf_total_sz > sizeof sbuf || rbuf_total_sz > sizeof rbuf) {
    return -E2BIG;
  }


  // TODO: Check total of IOVs < buf sizes

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EBADF;
  }

  sb = vnode->superblock;


//  if (is_allowed(vnode, R_OK) != 0) {
//    return -EACCES;
//  }

  nbytes_read = 0;  
  i=0;
  iov_remaining = siov[0].size;
    
  while (i < siov_cnt && nbytes_read < sbuf_total_sz) {
    buf_remaining = sbuf_total_sz - nbytes_read;
    nbytes_to_read = (buf_remaining < iov_remaining) ? buf_remaining : iov_remaining;

    CopyIn(sbuf + nbytes_read,
            siov[i].addr + siov[i].size - iov_remaining,
            nbytes_to_read);

    nbytes_read += nbytes_to_read;
    i++;
    iov_remaining = siov[i].size;
  }

  memset(&req, 0, sizeof req);
  req.cmd = CMD_SENDMSG;
  req.args.sendmsg.inode_nr = vnode->inode_nr;
  req.args.sendmsg.subclass = 0;
  req.args.sendmsg.ssize = sbuf_total_sz;
  req.args.sendmsg.rsize = rbuf_total_sz;

  msgsiov[0].addr = &req;
  msgsiov[0].size = sizeof req;
  msgsiov[1].addr = sbuf;
  msgsiov[1].size = sbuf_total_sz;  
  msgriov[0].addr = rbuf;
  msgriov[0].size = rbuf_total_sz;

  nbytes_response = ksendmsg(&sb->msgport, 2 , msgsiov, 1, msgriov);

  if (nbytes_response > 0) { 
    if (nbytes_response < rbuf_total_sz) {
      rbuf_total_sz = nbytes_response;
    }

    if (nbytes_response > rbuf_total_sz) {
      vnode_unlock(vnode);
      return -E2BIG;
    }

    if (riov_cnt > 0) {
      nbytes_written = 0;
      i=0;
      iov_remaining = riov[0].size;

      while (i < riov_cnt && nbytes_written < rbuf_total_sz) {
        buf_remaining = rbuf_total_sz - nbytes_written;
        nbytes_to_write = (iov_remaining < buf_remaining) ? iov_remaining : buf_remaining;

        int sc = CopyOut(riov[i].addr + riov[i].size - iov_remaining,
               rbuf + nbytes_written, nbytes_to_write);

        if (sc != 0) {
          nbytes_response = sc;
          break;
        }

        nbytes_written += nbytes_to_write;
        i++;
        iov_remaining = riov[i].size;
      }
    }
  }  

  vnode_unlock(vnode);
  return nbytes_response;
}





/*
 * TODO: Remove, if needed move fields into fsreq.
 * Modify receivemsg so that fsreq is received separately from IOVs.
 * Same for replymsg and fsreply
 */
int sys_getmsginfo(int fd, msgid_t msgid, msginfo_t *_mi)
{
  struct Process *current_proc;
  struct SuperBlock *sb;
  struct Msg *msg;
  msginfo_t mi;
  
  current_proc = get_current_process();  
  sb = get_superblock(current_proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  if ((msg = msgid_to_msg(&sb->msgbacklog, msgid)) == NULL) {
    return -EINVAL;
  }
  
  // TODO: Get info on message
  
  CopyOut(_mi, &mi, sizeof (msginfo_t));
  return 0;
}

 
/* @brief   Send a message to a message port and wait for a reply.
 *
 * TODO:  Need timeout and abort mechanisms into IPC in case server doesn't respond or terminates.
 * Also timeout works for when a server tries to create a second mount in its own mount path.
 */
int ksendmsg(struct MsgPort *msgport, int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov)
{
  struct Process *current_proc;
  struct Thread *current_thread;
  struct Msg msg;
	int sc;
	
  current_proc = get_current_process();
  current_thread = get_current_thread();
  
  msg.reply_port = &current_thread->reply_port;  
  msg.siov_cnt = siov_cnt;
  msg.siov = siov;
  msg.riov_cnt = riov_cnt;
  msg.riov = riov;  
  msg.reply_status = 0;
     
  kputmsg(msgport, &msg);   
  
  // TODO: We need a timeout/alarm and if it occurs we forcibly abort.

  while ((kwaitport(&current_thread->reply_port, NULL)) != 0) {
    // Check if message is on pending queue (not yet received by server)
    // If so, silently remove message, set msg._reply_status to -EINTR and return. 
      
    // TODO: Check signals, we may want to kill the current_proc process immediately.
   
    sc = kabortmsg(msgport, &msg);

    if (sc != 0) {
      return sc;
    }
  }

  kgetmsg(&current_thread->reply_port);
	
  return msg.reply_status;
}


/*
 * We also need to handle the case of the message port closing due to either closing
 * of the file handle or the server process terminating or the client being killed and
 * and so messages need an immediate disconnec+t.
 */
int kabortmsg(struct MsgPort *msgport, struct Msg *msg)
{
  struct fsreq *fsreq;
  
  if (msg->msgid != -1) {
    // message has been received and is assigned a msgid, send it again with CMD_ABORT
    // and no parameters. IOVs will remain the same so readmsg, writemsg and replymsg will be ok.
    // Make sure siov count >= 1.
    
    // Don't we want the same msgid when it's received a second time ? 
    
    fsreq = msg->siov[0].addr;
    
    if (fsreq->cmd == CMD_ABORT) {
      return -EBUSY;
    }
    
    fsreq->cmd = CMD_ABORT;

    msg->port = msgport;
    LIST_ADD_TAIL(&msgport->pending_msg_list, msg, link);
    knote(&msgport->knote_list, NOTE_MSG);
    
    return 0;
    
  } else if (msg->port == msgport) {
    // message has not been received, but is still on message list of server, remove it 
    kremovemsg(msgport, msg);
    msg->msgid = -1;
    msg->reply_status = -EINTR;
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
  msg->msgid = -1;
  msg->port = msgport;
  LIST_ADD_TAIL(&msgport->pending_msg_list, msg, link);

  knote(&msgport->knote_list, NOTE_MSG);  

  return 0;
}


/* @brief   Reply to a kernel message
 *
 * Note: The msgid in the message must be freed after calling kreplymsg.
 */
int kreplymsg(struct Msg *msg)
{
  struct MsgPort *reply_port;

  KASSERT (msg != NULL);  
  KASSERT (msg->reply_port != NULL);
  
  msg->msgid = -1;
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
        Warn("kwaitport sc=%d", sc);
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


/* @brief   Initialize a message backlog
 *
 * @param   msgbacklog, message backlog structure to initialize
 * @param		backlog, maximum number of concurrent messages to allow.
 * @return  0 on success, negative errno on error
 */
int init_msgbacklog(struct MsgBacklog *backlog, int backlog_sz)
{
	backlog->backlog_sz = backlog_sz;
	backlog->free_bitmap = 0;
	
	for (int t=0; t<backlog_sz; t++) {
		backlog->free_bitmap |= (1<<t);
	}
		
	for (int t=0; t< MAX_MSG_BACKLOG; t++) {
		backlog->msg[t] = NULL;
	}
	
  return 0;
}


/* @brief		Assign a msgid to a message in the backlog table
 *
 */
void assign_msgid(struct MsgBacklog *backlog, msgid_t msgid, struct Msg *msg)
{
	backlog->msg[msgid] = msg;
	msg->msgid = msgid;
}


/* @brief   Get a pointer to message from the message ID.
 *
 * @param   backlog, the message backlog the message is on
 * @param   msgid, message ID of the message to lookup.
 * @return  Pointer to message structure, or NULL on failure
 *
 * A named message port (mount point) allows a limited number of 
 * concurrent connections that are assigned when sys_getmsg is called.
 * A unique msgid is assigned from a pool of size "backlog", specified
 * when creating the msg port with sys_createmsgport().
 */
struct Msg *msgid_to_msg(struct MsgBacklog *backlog, msgid_t msgid)
{
  if (msgid < 0 || msgid >= backlog->backlog_sz) {
  	return NULL;
  }
  
  if ((backlog->free_bitmap & (1<<msgid)) != 0) {
  	return NULL;
  }
  
 return backlog->msg[msgid];
}


/*
 *
 */
msgid_t alloc_msgid(struct MsgBacklog *backlog)
{
	if (backlog->free_bitmap == 0) {
		return -1;
	}
	
	for (int t=0; t<backlog->backlog_sz; t++) {
		if ((backlog->free_bitmap & (1<<t)) != 0) {
			backlog->free_bitmap &= ~(1<<t);
			return t;
		}
	}
	
	return -1;
}


/*
 *
 */
void free_msgid(struct MsgBacklog *backlog, msgid_t msgid)
{
  if (msgid < 0 || msgid >= backlog->backlog_sz) {
  	return;
  }
  
  backlog->free_bitmap |= (1<<msgid);
  
  backlog->msg[msgid]->msgid = -1;
  backlog->msg[msgid] = NULL;
}


/* @brief   Seek to a position within a multi-part message
 *
 */
int seekiov(int iov_cnt, msgiov_t *iov, off_t offset, int *i, size_t *iov_remaining)
{
  off_t base_offset;

  base_offset = 0;
  
  // FIXME: We may not have an riov (riov) can be null

  for (*i = 0; *i < iov_cnt; (*i)++) {
    if (offset >= base_offset && offset < base_offset + iov[*i].size) {
      *iov_remaining = base_offset + iov[*i].size - offset;
      break;
    }

    base_offset += iov[*i].size;
  }

  if (*i >= iov_cnt) {
    return -EINVAL;
  }

  return 0;
}

