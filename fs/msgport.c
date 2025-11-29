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

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/utility.h>
#include <poll.h>
#include <string.h>
#include <sys/mount.h>


/* @brief   Create a named msgport (mount point) in the file system namespace
 *
 * @param   _path, path to mount root of new filesystem at
 * @param   _stat, configuration settings to apply to this msgport (mount point).
 * @return  file descriptor on success, negative errno on failure
 *
 * This mounts a filesystem on top of an existing vnode. A file handle
 * is returned that points to this mount. A server can then use this
 * handle to receive and process messages from the kernel's virtual file
 * system. The sys_getmsg, sys_replymsg, sys_readmsg and sys_writemsg
 * system calls are available to servers to process file system requests.
 *
 * TODO: Atomically create node on filesystem and mount this message port on it.
 * Avoids need for 2 steps and possible race conditions during mount.
 *
 * TODO: Do we need to lock the vnodes ?
 * 
 */ 
int sys_createmsgport(char *_path, uint32_t flags, struct stat *_stat, pid_t tid, int event)
{
  struct lookupdata ld;
  struct stat stat;
  struct Process *current;
  struct VNode *vnode_covered = NULL;
  struct VNode *mount_root_vnode = NULL;
  struct SuperBlock *sb = NULL;
  struct Filp *filp = NULL;
  struct FileDesc *filedesc;
  int fd = -1;
  int sc;
  bool do_lookup_cleanup;
    
  Info("sys_createmsgport()");

  current = get_current_process();

  if (CopyIn(&stat, _stat, sizeof stat) != 0) {
    return -EFAULT;
  }

  if (root_vnode != NULL) {
    if ((sc = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
      Error("createmsgport failed: sc: %d", sc);
      return sc;
    }

    do_lookup_cleanup = true;

    vnode_covered = ld.vnode;

    if (vnode_covered == NULL) {
      lookup_cleanup(&ld);
      Error("createmsgport failed: vnode_covered = null -ENOENT");
      return  -ENOENT;
    }

    Info("createmsgport: vnode covered mode : %0o", vnode_covered->mode);
    Info("createmsgport: stat.st_mode : %0o", stat.st_mode);
    
    if (! ((S_ISDIR(stat.st_mode) && S_ISDIR(vnode_covered->mode))
        || (S_ISCHR(stat.st_mode) && S_ISCHR(vnode_covered->mode))
        || (S_ISBLK(stat.st_mode) && S_ISBLK(vnode_covered->mode)))) {
      lookup_cleanup(&ld);
      Error("createmsgport failed: not dir, chr or blk, -EINVAL ******************");
      return -EINVAL;
    }
    
    // TODO: Check covered permissions ?  

    if (vnode_covered->vnode_covered != NULL) {
      lookup_cleanup(&ld);
      Error("createmsgport failed: vnode_covered != NULL, -EEXIST");
      return -EEXIST;
    }
  } else { 
    // TODO: Check mount path is "/" only,  check we are superuser
    do_lookup_cleanup = false;
    vnode_covered = NULL; 
  }

  fd = fd_alloc(current, 0, FILEDESC_MAX, &filedesc);
  
  if (fd < 0) {
    Error("createmsgport failed: no free FDs -EMFILE");
    return -EMFILE;
  }
  
  filp = filp_get_new();
  
  if (filp == NULL) {
    fd_free(current, fd);
    Error("createmsgport failed: no free filps -EMFILE");
    return -EMFILE;
  }
    
  sb = alloc_superblock();
  
  if (sb == NULL) {
    fd_free(current, fd);
    filp_release(filp);
    Error("createmsgport failed: no free superblocks -EMFILE");
    return -EMFILE;
  }
     
  if (fd < 0) {
    Error("createmsgport failed to alloc file descriptor, -ENOMEM");
    if (do_lookup_cleanup) {
      lookup_cleanup(&ld);
    }
    
    // FIXME: No freeing of fd, filp or superblock
    return -ENOMEM;
  }
  
  mount_root_vnode = vnode_get_new(sb);

  if (mount_root_vnode == NULL) {
    Error("createmsgport failed to alloc vnode, -ENOMEM");
// FIXME:   fd_free(current, fd);
// FIXME:   filp_release(fd);
    
    if (do_lookup_cleanup) {
      lookup_cleanup(&ld);
    }

    return -ENOMEM;
  }

  init_msgport(&sb->msgport, tid, event);
  
  sb->msgport.context = sb;

  sb->root = mount_root_vnode;
  sb->flags = flags;
  sb->reference_cnt = 1;          // 1 reference count of the root vnode (maybe also handle?)
  
  sb->dev = stat.st_dev;
   
  mount_root_vnode->inode_nr = stat.st_ino;
  mount_root_vnode->uid = stat.st_uid;
  mount_root_vnode->gid = stat.st_gid;
  mount_root_vnode->mode = stat.st_mode;    
  mount_root_vnode->flags = V_VALID | V_ROOT;

  vnode_ref(mount_root_vnode);
    
  vnode_hash_enter(mount_root_vnode);
  
  if (S_ISBLK(mount_root_vnode->mode)) {    
    mount_root_vnode->size = (off64_t)stat.st_blocks * (off64_t)stat.st_blksize;

    // FIXME: Could we use blocks and blkize instead of vnode->size?
    // Add these fields to vnode?
    //  blksize_t     st_lksize;
    //  blkcnt_t	st_blocks;
  
  } else {
    mount_root_vnode->size = stat.st_size;
  }
    
  mount_root_vnode->vnode_covered = vnode_covered;

  if (root_vnode == NULL) {
    root_vnode = mount_root_vnode;
  }    
  
  if (vnode_covered != NULL) {
    vnode_covered->vnode_mounted_here = mount_root_vnode;
    vnode_ref(vnode_covered);
  }

  vnode_ref(mount_root_vnode);

  if (do_lookup_cleanup) {
    lookup_cleanup(&ld);
  }

  Info("createmsgport : FILP_TYPE_SUPERBLOCK, fd:%d", fd);
  
  filp->type = FILP_TYPE_SUPERBLOCK;
  filp->u.superblock = sb;
  filp->offset = 0;
  filp->flags = O_RDONLY | O_WRONLY;
    
  filedesc->filp = filp;
  filedesc->flags |= FDF_VALID;

  Info("createmsgport returning fd:%d", fd);
  return fd;
}


/* @brief   Unmount a filesystem or device
 *
 * @param   _path, pathname to the filesystem to unmount
 * @param   flags, options to control how the filesystem is unmounted
 * @return  0 on success, negative errno on failure
 */
int sys_unmount(char *_path, uint32_t flags)
{
  // TODO:  Lookup pathname,
  // flag this volume for unmounting, prevent further use.

  // TODO: Should this wait, with a timeout ?
  // If reference count is zero, remove detach from mount point
  
  // Call syncfs() ?   BLOCKING ?
  
  // Common code with close_msgport
  // Free all buffers
  // Free all vnodes
  // Free superblock

  return -ENOSYS;
}


/* @brief   Initialize a message port
 *
 * @param   msgport, message port to initialize
 * @return  0 on success, negative errno on error
 */
int init_msgport(struct MsgPort *msgport, pid_t tid, int event)
{
  LIST_INIT(&msgport->pending_msg_list);
  LIST_INIT(&msgport->received_msg_list);
  
  InitRendez(&msgport->rendez);
  
  msgport->context = NULL;
  msgport->flags = 0;
  msgport->target_tid = tid;
  msgport->target_event = event;
  
  return 0;
}


/* @brief   Abort pending and received messages sent to a message port
 *
 * @param   msgport, message port to cleanup
 * @return  0 on success, negative errno on error
 *
 * Removes any pending and received messages and places these on the messages's
 * reply ports.
 
 * NOTE: Message port will be freed elsewhere. Therefore any messages or
 * should not rely on the message port still existing.
 */
int fini_msgport(struct MsgPort *port)
{
  struct Msg *msg;
  
  port->flags |= MPF_SHUTDOWN;
    
  while ((msg = LIST_HEAD(&port->received_msg_list)) != NULL) {
    LIST_REM_HEAD(&port->received_msg_list, link);

    msg->msgid = INVALID_PID;
    msg->reply_status = -ECONNABORTED;      
    msg->port = msg->reply_port;
    
    LIST_ADD_TAIL(&msg->reply_port->pending_msg_list, msg, link);
    TaskWakeup(&msg->reply_port->rendez);   // FIXME: Is this used?  
  }
  
  while ((msg = LIST_HEAD(&port->pending_msg_list)) != NULL) {
    LIST_REM_HEAD(&port->pending_msg_list, link);

    msg->msgid = INVALID_PID;
    msg->reply_status = -ECONNABORTED;      
    msg->port = msg->reply_port;
    
    LIST_ADD_TAIL(&msg->reply_port->pending_msg_list, msg, link);
    TaskWakeup(&msg->reply_port->rendez);   // FIXME: Is this used?
  }
  
  return 0;
}


