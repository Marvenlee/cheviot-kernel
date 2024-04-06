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

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <kernel/kqueue.h>


/* @brief   Send a knote event to a vnode in the kernel
 *
 * @param   fd, file descriptor of mount on which the file exists
 * @param   ino_nr, inode number of the file
 * @param   hint, hint of why this is being notified. *
 * @return  0 on success, negative errno on error
 *
 * Perhaps change it to sys_setvnodeattrs(fd, ino_nr, flags);
 */
int sys_knotei(int fd, int ino_nr, long hint)
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct Process *current;
  
  current = get_current_process();  
  sb = get_superblock(current, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  vnode = vnode_get(sb, ino_nr);
  
  if (vnode == NULL) {
    return -EINVAL;
  }
    
  knote(&vnode->knote_list, hint);  
  vnode_put(vnode);
  return 0;
}


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
  struct Process *current;

  current = get_current_process();  
  sb = get_superblock(current, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  msgport = &sb->msgport;
  
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
  struct Process *current;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_write;
  int nbytes_written;
  int remaining;
  int iov_remaining;
  int i;
  int sc;
    
  current = get_current_process();  
  sb = get_superblock(current, fd);

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
  
  if (msg->reply_port != NULL) {
  	kreplymsg(msg);
 	} else {
 		bdflush_brelse(msg);
 	}
 	
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
  struct Process *current;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_read;
  int nbytes_read;
  int buf_remaining;
  int iov_remaining;
  int i;
    
  current = get_current_process();  
  sb = get_superblock(current, fd);
  
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
  struct Process *current;
  struct SuperBlock *sb;
  struct MsgPort *msgport;
  struct Msg *msg;
  int nbytes_to_write;
  int nbytes_written;
  int buf_remaining;
  int iov_remaining;
  int i;
  int sc;

  
  current = get_current_process();  
  sb = get_superblock(current, fd);
  
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
 */
int sys_sendrec(int fd, int siov_cnt, struct IOV *siov, int riov_cnt, struct IOV *riov)
{
  return -ENOSYS;
}

 
/* @brief   Send a message to a message port and wait for a reply.
 *
 * TODO:  Need timeout and abort mechanisms into IPC in case server doesn't respond or terminates.
 * Also timeout works for when a server tries to create a second mount in its own mount path.
 */
int ksendmsg(struct MsgPort *msgport, int siov_cnt, struct IOV *siov, int riov_cnt, struct IOV *riov)
{
  struct Process *current;
  struct Msg msg;
	
  current = get_current_process();
      
  msg.reply_port = &current->reply_port;  
  msg.siov_cnt = siov_cnt;
  msg.siov = siov;
  msg.riov_cnt = riov_cnt;
  msg.riov = riov;  
  msg.reply_status = 0;
     
  kputmsg(msgport, &msg);   
  kwaitport(&current->reply_port, NULL);  
  kgetmsg(&current->reply_port);
	
  return msg.reply_status;
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


/* @brief   Wait for a message port to receive a message
 *
 * @param   msgport, message port to wait on
 * @param   timeout, duration to wait for a message
 * @return  0 on success,
 *          -ETIMEDOUT on timeout
 *          negative errno on other failure
 */
int kwaitport(struct MsgPort *msgport, struct timespec *timeout)
{
  while (LIST_HEAD(&msgport->pending_msg_list) == NULL) {
    if (timeout == NULL) {
      TaskSleep(&msgport->rendez);
    } else {    
      if (TaskTimedSleep(&msgport->rendez, timeout) == -ETIMEDOUT) {
        return -ETIMEDOUT;  
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
  backlog->msg[msgid] = NULL;
}


/* @brief   Seek to a position within a multi-part message
 *
 */
int seekiov(int iov_cnt, struct IOV *iov, off_t offset, int *i, size_t *iov_remaining)
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

