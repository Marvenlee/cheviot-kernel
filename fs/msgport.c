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
 * system. The sys_kevent, sys_getmsg, sys_replymsg, sys_readmsg and
 * sys_writemsg system calls are available to servers to process file
 * system requests.
 *
 * TODO: Atomically create node on filesystem and mount this message port on it.
 * Avoids need for 2 steps and possible race conditions during mount.
 *
 * TODO: Do we need to lock the vnodes ?
 * 
 */ 
int sys_createmsgport(char *_path, uint32_t flags, struct stat *_stat)
{
  struct lookupdata ld;
  struct stat stat;
  struct Process *current;
  struct VNode *vnode_covered = NULL;
  struct VNode *mount_root_vnode = NULL;
  struct Filp *filp = NULL;
  struct SuperBlock *sb = NULL;
  struct MsgPort *msgport;
  int fd = -1;
  int sc;
  bool do_lookup_cleanup;
    
  Info("sys_createmsgport");

  current = get_current_process();

  if (CopyIn(&stat, _stat, sizeof stat) != 0) {
    return -EFAULT;
  }

  if (root_vnode != NULL) {
    if ((sc = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
      return sc;
    }

    do_lookup_cleanup = true;

    vnode_covered = ld.vnode;

    if (vnode_covered == NULL) {
      lookup_cleanup(&ld);
      return  -ENOENT;
    }

    if (! ((S_ISDIR(stat.st_mode) && S_ISDIR(vnode_covered->mode))
        || (S_ISCHR(stat.st_mode) && S_ISCHR(vnode_covered->mode))
        || (S_ISBLK(stat.st_mode) && S_ISBLK(vnode_covered->mode)))) {
      lookup_cleanup(&ld);
      return -EINVAL;
    }
    
    // TODO: Check covered permissions ?  

    if (vnode_covered->vnode_covered != NULL) {
      lookup_cleanup(&ld);
      return -EEXIST;
    }
  } else { 
    // TODO: Check mount path is "/" only,  check we are superuser
    do_lookup_cleanup = false;
    vnode_covered = NULL; 
  }


  rwlock(&superblock_list_lock, LK_EXCLUSIVE);

  fd = alloc_fd_superblock(current);
  
  if (fd < 0) {
    Error("createmsgport failed to alloc file descriptor");
    rwlock(&superblock_list_lock, LK_RELEASE);
    if (do_lookup_cleanup) {
      lookup_cleanup(&ld);
    }
    return -ENOMEM;
  }
  
  sb = get_superblock(current, fd);
  
  mount_root_vnode = vnode_new(sb);

  if (mount_root_vnode == NULL) {
    Error("createmsgport failed to alloc vnode");
    free_fd_superblock(current, fd);
    rwlock(&superblock_list_lock, LK_RELEASE);
    
    if (do_lookup_cleanup) {
      lookup_cleanup(&ld);
    }
    return -ENOMEM;
  }

  init_msgport(&sb->msgport);
  sb->msgport.context = sb;

  sb->root = mount_root_vnode;
  sb->flags = flags;
  sb->reference_cnt = 1;

  sb->dev = stat.st_dev;
   
  mount_root_vnode->inode_nr = stat.st_ino;
  mount_root_vnode->reference_cnt = 1;
  mount_root_vnode->uid = stat.st_uid;
  mount_root_vnode->gid = stat.st_gid;
  mount_root_vnode->mode = stat.st_mode;    
  mount_root_vnode->flags = V_VALID | V_ROOT;

  vnode_hash_enter(mount_root_vnode);

  if (S_ISDIR(mount_root_vnode->mode) && (sb->flags & SF_READONLY) == 0) {
    if (init_superblock_bdflush(sb) != 0) {
      vnode_discard(mount_root_vnode);
      free_fd_superblock(current, fd);
      rwlock(&superblock_list_lock, LK_RELEASE);
      if (do_lookup_cleanup) {
        lookup_cleanup(&ld);
      }
      return -ENOMEM;
    }

  }
  
  if (S_ISBLK(mount_root_vnode->mode)) {    
    mount_root_vnode->size = (off64_t)stat.st_blocks * (off64_t)stat.st_blksize;

    // FIXME: Could we use blocks and blkize instead of vnode->size?
    // Add these fields to vnode?
    //  blksize_t     st_blksize;
    //  blkcnt_t	st_blocks;
  
  } else {
    mount_root_vnode->size = stat.st_size;
  }
    
  mount_root_vnode->vnode_covered = vnode_covered;
  knote(&mount_root_vnode->knote_list, NOTE_ATTRIB);

  if (root_vnode == NULL) {
    root_vnode = mount_root_vnode;
  }    
  
  if (vnode_covered != NULL) {
    vnode_covered->vnode_mounted_here = mount_root_vnode;
    vnode_add_reference(vnode_covered);
    
    knote(&vnode_covered->knote_list, NOTE_ATTRIB);
  }

  vnode_add_reference(mount_root_vnode);

  rwlock(&superblock_list_lock, LK_RELEASE);

  if (do_lookup_cleanup) {
    lookup_cleanup(&ld);
  }
  
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

  rwlock(&superblock_list_lock, LK_EXCLUSIVE);

  // TODO: Should this wait, with a timeout ?
  // If reference count is zero, remove detach from mount point
  
  // Call syncfs() ?   BLOCKING ?
  
  // Common code with close_msgport
  // Free all buffers
  // Free all vnodes
  // Free superblock

  rwlock(&superblock_list_lock, LK_RELEASE);

  return -ENOSYS;
}


/*
 * Need to have separate function to sync the device,
 * flush all pending delayed writes and anything in message queue
 * Prevent further access
 *
 * Do we leave it to close_vnode() to check if the superblock needs cleaning
 * up if there is no servers referencing it?
 */ 
int close_msgport(struct Process *proc, int fd)
{
  struct SuperBlock *sb;
  struct VNode *vnode_covered;

  Info("close_msgport()");
  
  KASSERT(proc != NULL);
  
  sb = get_superblock(proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }

  // TODO: Close the message port, if there are no vnodes active free the superblock
  // and unmount.
  
  // TODO: Read-only filesystems don't need delayed writes so no need to sync
  // or stop bdflush task.
  
  return 0;
}



