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
  int sc;
  bool do_lookup_cleanup;
    
  Info("sys_createmsgport");

  current = get_current_process();

  if (CopyIn(&stat, _stat, sizeof stat) != 0) {
    Error("createmsgport copyin failed _stat :%08x", (uint32_t)_stat);
    return -EFAULT;
  }

  if (root_vnode != NULL) {
    if ((sc = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
      Error("createmsgport lookup failed");
      return sc;
    }

    do_lookup_cleanup = true;

    vnode_covered = ld.vnode;

    if (vnode_covered == NULL) {
      Error("createmsgport vnode_covered == NULL");
      lookup_cleanup(&ld);
      return  -ENOENT;
    }

    if (! ((S_ISDIR(stat.st_mode) && S_ISDIR(vnode_covered->mode))
        || (S_ISCHR(stat.st_mode) && S_ISCHR(vnode_covered->mode))
        || (S_ISBLK(stat.st_mode) && S_ISBLK(vnode_covered->mode)))) {
      Error("createmsgport invalid mount mode");
      lookup_cleanup(&ld);
      return -EINVAL;
    }
    
    // TODO: Check covered permissions ?  

    if (vnode_covered->vnode_covered != NULL) {
      Error("createmsgport already mount point");
      lookup_cleanup(&ld);
      return -EEXIST;
    }
  } else { 
    // TODO: Check mount path is "/" only,  check we are superuser
    do_lookup_cleanup = false;
    vnode_covered = NULL; 
  }

  fd = alloc_fd_superblock(current);
  
  if (fd < 0) {
    Error("createmsgport failed to alloc file descriptor");
    lookup_cleanup(&ld);
    return -ENOMEM;
  }
  
  sb = get_superblock(current, fd);

  Info("sb = %08x", (uint32_t)sb);
  
  mount_root_vnode = vnode_new(sb);

  Info("vnode_new -> mount_root_vnode = %08x", (uint32_t)mount_root_vnode);

  Info("a mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);


  if (mount_root_vnode == NULL) {
    Error("createmsgport failed to alloc vnode");
    free_fd_superblock(current, fd);
    lookup_cleanup(&ld);
    return -ENOMEM;
  }

  InitRendez(&sb->rendez);

  init_msgport(&sb->msgport);
  sb->msgport.context = sb;

  sb->root = mount_root_vnode;
  sb->flags = flags;
  sb->reference_cnt = 1;
  sb->busy = false;

  sb->dev = stat.st_dev;
   
  mount_root_vnode->inode_nr = stat.st_ino;
  mount_root_vnode->reference_cnt = 1;
  mount_root_vnode->uid = stat.st_uid;
  mount_root_vnode->gid = stat.st_gid;
  mount_root_vnode->mode = stat.st_mode;    
  mount_root_vnode->flags = V_VALID | V_ROOT;
  vnode_hash(mount_root_vnode);

  // TODO: Read-only filesystems don't need delayed writes
  if (S_ISDIR(mount_root_vnode->mode)) {
    if (init_superblock_bdflush(sb) != 0) {
      vnode_discard(mount_root_vnode);
      free_fd_superblock(current, fd);
      lookup_cleanup(&ld);
      return -ENOMEM;
    }
  }

  Info("b mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);
  
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

  Info("c mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);


  if (root_vnode == NULL) {
    root_vnode = mount_root_vnode;
    Info("set root_vnode to: %08x", (uint32_t)root_vnode);
  }    

  Info("d mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);
  
  if (vnode_covered != NULL) {
    vnode_covered->vnode_mounted_here = mount_root_vnode;
    knote(&vnode_covered->knote_list, NOTE_ATTRIB);

    vnode_add_reference(vnode_covered);
    vn_lock(vnode_covered, VL_RELEASE);
  }

  Info("e mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);

  vnode_add_reference(mount_root_vnode);
  vn_lock(mount_root_vnode, VL_RELEASE);

  Info("f mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);

  Info("createmsgport calling lookup_cleanup");

  if (do_lookup_cleanup) {
    lookup_cleanup(&ld);
  }
  
  Info("g mount_root_vnode->superblock=%08x", (uint32_t)mount_root_vnode->superblock);
  
  Info("createmsgport returning fd:%d", fd);
  return fd;
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

  Info("close_msgport()");
  
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



