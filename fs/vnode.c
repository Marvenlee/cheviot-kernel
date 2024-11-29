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
 * Vnode handling.
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <string.h>
#include <kernel/kqueue.h>


/* @brief   Lookup a vnode from a file descriptor
 *
 * @param   proc, process in which the file descriptor belongs
 * @param   fd, file descriptor to find vnodce of
 * @return  vnode if found, otherwise returns NULL
 */
struct VNode *get_fd_vnode(struct Process *proc, int fd)
{
  struct Filp *filp;

  if (fd < 0 || fd >= OPEN_MAX) {
    return NULL;
  }
  
  filp = proc->fproc->fd_table[fd];
    
  if (filp == NULL) {
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_VNODE) {
    return NULL;
  }
  
  return filp->u.vnode;
}


/* @brief   Close a file descriptor reference to a open file
 *
 * @param   proc, process in which the file descriptor belongs
 * @param   fd, file descriptor of file to close
 * @return  0 on success, negative errno on failure
 *
 * FIXME: on vnode_put, we need to send a vfs_close() on zero reference_count
 *        Same here?
 */
int close_vnode(struct Process *proc, int fd)
{   
  struct Filp *filp;
  struct Pipe *pipe;
  struct VNode *vnode;

  filp = get_filp(proc, fd);
  vnode = get_fd_vnode(proc, fd);
  
  if (vnode == NULL) {
    return -EINVAL;  
  }

  vnode_put(vnode);
  free_fd_filp(proc, fd);
  
  return 0;
}


/* @brief   Allocate a new vnode
 *
 * Allocate a new vnode object, assign an inode_nr to it and lock it.
 * A call to vnode_find() should be called prior to this to see if the vnode
 * already exists.
 *
 * If there are no vnodes available this returns NULL.
 *
 * This may block while freeing existing vnode and flushing its blocks to disk.
 */
struct VNode *vnode_new(struct SuperBlock *sb)
{
  struct VNode *vnode;

  Info("vnode_new(sb:%08x)", (uint32_t)sb);

  vnode = LIST_HEAD(&vnode_free_list);

  if (vnode == NULL) {
    return NULL;
  }

  LIST_REM_HEAD(&vnode_free_list, vnode_entry);

  if (vnode->flags & V_VALID) {
    vn_lock(vnode, VL_EXCLUSIVE);
    bsync(vnode);
    vn_lock(vnode, VL_RELEASE);
    vnode->flags = 0;
  }
  
  sb->reference_cnt++;
  
  InitRendez(&vnode->rendez);

  vn_lock_init(&vnode->vlock);
  
  vnode->inode_nr = -1;
  vnode->reference_cnt = 1;

  vnode->superblock = sb;
  vnode->flags = 0;

  vnode->char_read_busy = false;
  vnode->char_write_busy = false;
    
  vnode->vnode_mounted_here = NULL;
  vnode->vnode_covered = NULL;
  vnode->pipe = NULL;
  
  vnode->tty_sid = INVALID_PID;
    
  vnode->mode = 0;
  vnode->uid = 9999;
  vnode->gid = 9999;
  vnode->size = 0;
  vnode->atime = 0;
  vnode->mtime = 0;
  vnode->ctime = 0;
  vnode->blocks = 0;
  vnode->blksize = 512;  // default to 512
  vnode->rdev = 0;   // FIXME: rdev
  vnode->nlink = 0;  // hard links count
    
  LIST_INIT(&vnode->buf_list);
  LIST_INIT(&vnode->vnode_list);
  LIST_INIT(&vnode->directory_list);
  LIST_INIT(&vnode->knote_list);
  
  return vnode;
}


/* @brief   Place vnode in hash table for quicker lookups
 *
 * TODO;
 */
void vnode_hash(struct VNode *vnode)
{
}


/* @brief   Find an existing vnode
 */
struct VNode *vnode_get(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;

  if (sb->flags & S_ABORT) {
    return NULL;
  }
  
  if ((vnode = vnode_find(sb, inode_nr)) != NULL) {
    vnode->reference_cnt++;
    sb->reference_cnt++;
  
    if ((vnode->flags & V_FREE) == V_FREE) {
      LIST_REM_ENTRY(&vnode_free_list, vnode, vnode_entry);
    }

    return vnode;
  
  } else {
    return NULL;
  }
}


/*
 * Used to increment reference count of existing VNode.  Used within FChDir so
 * that proc->current_dir counts as reference.
 *
 */
void vnode_add_reference(struct VNode *vnode)
{
  vnode->reference_cnt++;
  vnode->superblock->reference_cnt++;
}


/*
 * @brief   Release a VNode
 *
 * VNode is returned to the cached pool where it can lazily be freed.
 * 
 * FIXME: on vnode_put, we need to send a vfs_close() on zero reference_count
 */
void vnode_put(struct VNode *vnode)
{
  KASSERT(vnode != NULL);
 
  Info("vnode_put() - sb:%08x", (uint32_t)vnode->superblock);

  KASSERT(vnode->superblock != NULL);

  // Assert it is not locked.
  
  vnode->reference_cnt--;  
  vnode->superblock->reference_cnt--;
  
  if (vnode->reference_cnt == 0) {
    
#if 0
    if (vnode->nlink == 0) {
    // FIXME: IF vnode->link_count == 0,  delete entries in cache.
    // FIXME: Do any special cleanup of file, dir, fifo ?
    // TODO: Mark vnode as free/invalid    
    }  
#endif

    if ((vnode->flags & V_ROOT) == 0) {
      vnode->flags |= V_FREE;
      LIST_ADD_TAIL(&vnode_free_list, vnode, vnode_entry);
    }
  }
    
  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Discard a vnode, put it on the free list and mark it as invalid.
 */
void vnode_discard(struct VNode *vnode)
{
  // TODO: assert vnode is not null
  // TODO: assert that vnode lock is in DRAIN state
  // TODO: Remove from any lookup hash list
  // Reset the vnode lock;
  
  vnode->flags = V_FREE;
  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_entry);
  vnode->reference_cnt = 0;

  vn_lock_init(&vnode->vlock);

  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Find an existing vnode in the vnode cache
 *
 * TODO : Hash vnode by sb and inode_nr
 */
struct VNode *vnode_find(struct SuperBlock *sb, int inode_nr)
{
  int v;

  for (v = 0; v < NR_VNODE; v++) {
    if (/* FIXME:????? (vnode_table[v].flags & V_FREE) != V_FREE &&*/
        vnode_table[v].superblock == sb &&
        vnode_table[v].inode_nr == inode_nr) {
      return &vnode_table[v];
    }
  }
  return NULL;
}


/* @brief   VNode lock acquisition and release
 *
 */
int vn_lock(struct VNode *vnode, int flags)
{
  int request;
  
  request = flags & VN_LOCK_REQUEST_MASK;
  
  switch(request) {
    case VL_EXCLUSIVE:
      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }

      while (vnode->vlock.exclusive_cnt != 0 || vnode->vlock.share_cnt != 0) {
        TaskSleep(&vnode->vlock.rendez);
      }

      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }
      
      vnode->vlock.exclusive_cnt = 1;
      break;

    case VL_SHARED:
      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }

      while (vnode->vlock.exclusive_cnt == 1) {
        TaskSleep(&vnode->vlock.rendez);
      }

      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }
      
      vnode->vlock.share_cnt++;
      break;

    case VL_UPGRADE:
      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }

      if (vnode->vlock.exclusive_cnt == 1) {
        return -EINVAL;
      }
    
      if (vnode->vlock.share_cnt > 0) {
        vnode->vlock.share_cnt--;
      }
            
      while(vnode->vlock.share_cnt != 0 || vnode->vlock.exclusive_cnt != 0) {
        TaskSleep(&vnode->vlock.rendez);
      }

      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }

      vnode->vlock.exclusive_cnt = 1;
      break;

    case VL_DOWNGRADE:
      if (vnode->vlock.exclusive_cnt == 1) {
        vnode->vlock.exclusive_cnt = 0;
        vnode->vlock.share_cnt++;
      } else {
        return -EINVAL;
      }            
      break;

    case VL_RELEASE:
      if (vnode->vlock.share_cnt > 0) {
        vnode->vlock.share_cnt--;
      } else if (vnode->vlock.exclusive_cnt == 1) {
        vnode->vlock.exclusive_cnt = 0;
      }
      
      if (vnode->vlock.exclusive_cnt == 0 || vnode->vlock.share_cnt == 0) {
        TaskWakeupAll(&vnode->vlock.rendez);
      }
      break;

    case VL_DRAIN:
      if (vnode->vlock.is_draining == true) {
        return -EINVAL;
      }
      
      vnode->vlock.is_draining = true;
      while(vnode->vlock.exclusive_cnt != 0 && vnode->vlock.share_cnt != 0) {
        TaskSleep(&vnode->vlock.rendez);
      }
      break;

    default:
      return -EINVAL;      
  }
  
  return 0;
}


/* @brief   VNode lock initialization
 *
 */
int vn_lock_init(struct VLock *vlock)
{
  vlock->is_draining = false;
  vlock->share_cnt = 0;
  vlock->exclusive_cnt = 0;
  InitRendez(&vlock->rendez);  
  return 0;
}

