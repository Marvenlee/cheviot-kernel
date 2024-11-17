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
  int error = -ENOTSUP;
  
  Info("sys_createmsgport");

  current = get_current_process();

  if (CopyIn(&stat, _stat, sizeof stat) != 0) {
    Error("createmsgport copyin failed _stat :%08x", (uint32_t)_stat);
    return -EFAULT;
  }

  if (root_vnode != NULL) {
    if ((error = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
      Error("createmsgport lookup failed");
      return error;
    }

    vnode_covered = ld.vnode;

    if (vnode_covered == NULL) {
      error = -ENOENT;
      Error("createmsgport vnode_covered == NULL");
      goto exit;
    }

    if (! ((S_ISDIR(stat.st_mode) && S_ISDIR(vnode_covered->mode))
        || (S_ISCHR(stat.st_mode) && S_ISCHR(vnode_covered->mode))
        || (S_ISBLK(stat.st_mode) && S_ISBLK(vnode_covered->mode)))) {
      errno = -EINVAL;
      Error("createmsgport invalid mount mode");

      goto exit;
    }
    
    // TODO: Check covered permissions ?  

    dname_purge_vnode(vnode_covered);

    if (vnode_covered->vnode_covered != NULL) {
      error = -EEXIST;
      Error("createmsgport already mount point");

      goto exit;
    }

  } else { 
    // TODO: Check mount path is "/" only
    vnode_covered = NULL; 
  }

  fd = alloc_fd_superblock(current);
  
  if (fd < 0) {
    Error("createmsgport failed to alloc file descriptor");

    error = -ENOMEM;
    goto exit;
  }
  
  sb = get_superblock(current, fd);
  
  mount_root_vnode = vnode_new(sb, stat.st_ino);

  if (mount_root_vnode == NULL) {
    Error("createmsgport failed to alloc vnode");

    error = -ENOMEM;
    goto exit;    
  }

  InitRendez(&sb->rendez);

  init_msgport(&sb->msgport);
  sb->msgport.context = sb;

  sb->root = mount_root_vnode;
  sb->flags = flags;
  sb->reference_cnt = 1;
  sb->busy = false;

  // TODO: dev_ stat.st_dev (major and minor numbers)
  // Check major/minor numbers, are allocated by current process.

  sb->dev = stat.st_dev;

  Info("mount() stat.st_dev = %08x", stat.st_dev);  
  Info("mount() sb->dev = %08x", sb->dev);
  
  mount_root_vnode->flags = V_VALID | V_ROOT;
  mount_root_vnode->reference_cnt = 1;
  mount_root_vnode->uid = stat.st_uid;
  mount_root_vnode->gid = stat.st_gid;
  mount_root_vnode->mode = stat.st_mode;
  
  
  // TODO: Read-only filesystems don't need delayed writes
  if (S_ISDIR(mount_root_vnode->mode)) {
    init_superblock_bdflush(sb);
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
    Info("set root_vnode to: %08x", (uint32_t)root_vnode);
  }    
  
  if (vnode_covered != NULL) {
    vnode_covered->vnode_mounted_here = mount_root_vnode;
    knote(&vnode_covered->knote_list, NOTE_ATTRIB);

    vnode_inc_ref(vnode_covered);
    vn_lock(vnode_covered, VL_RELEASE);
  }

  vnode_inc_ref(mount_root_vnode);
  vn_lock(mount_root_vnode, VL_RELEASE);
  
  return fd;

exit:

  // FIXME: Need to understand/cleanup what vnode get/put/free/ alloc? do
  //  vnode_put(server_vnode); // FIXME: Not a PUT?  Removed below?
  //  free_msgportid(portid);

  Error("Mount: failed %d", error);

  vnode_put(ld.vnode);  
  vnode_free(mount_root_vnode);
  free_fd_superblock(current, fd);
  vnode_put(vnode_covered);
  lookup_cleanup(&ld);
  return error;
}



/*
 * Need to have separate function to sync the device,
 * flush all pending delayed writes and anything in message queue
 * Prevent further access
 */ 
int close_msgport(struct Process *proc, int fd)
{
  struct SuperBlock *sb;
  struct VNode *vnode_covered;
  
  KASSERT(proc != NULL);
  
  sb = get_superblock(proc, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }

  /* FIXME: Check refernce count is zero when finally freeing
  
  vnode_covered = sb->vnode_covered;

  vnode_mounted_here = ;
  
  invalidate_superblock_blks(sb);
  invalidate_dnlc_entries(sb);
  invalidate_vnodes(sb);

  // TODO: Read-only filesystems don't need delayed writes

  if (S_ISDIR(vnode_mounted_here->mode)) {
    fini_superblock_bdflush(sb);
  }

  vnode_covered->vnode_mounted_here = NULL;

  */
  
  free_fd_superblock(proc, fd);

  return 0;
}



