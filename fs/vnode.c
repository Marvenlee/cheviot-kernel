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

//  Info("get_fd_vnode(proc:%08x, fd:%d)", (uint32_t)proc, fd);

  KASSERT(proc != NULL);

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

  Info("close_vnode(proc:%08x, fd:%d", (uint32_t)proc, fd);

  KASSERT(proc != NULL);

  filp = get_filp(proc, fd);
  vnode = get_fd_vnode(proc, fd);
  
  if (vnode == NULL) {
    return -EINVAL;  
  }

  if (S_ISREG(vnode->mode)) {
    Info("sync on close");
    bsync(vnode, BSYNC_ALL_NOW);
    
    
  } else if (S_ISFIFO(vnode->mode)) {
    pipe = vnode->pipe;
    if (filp->flags & O_RDONLY) {
      pipe->reader_cnt--;
    } else {
      pipe->writer_cnt--;
    }
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

  KASSERT(sb != NULL);

  vnode = LIST_HEAD(&vnode_free_list);

  if (vnode == NULL) {
    return NULL;
  }

  LIST_REM_HEAD(&vnode_free_list, vnode_link);

  if (vnode->flags & V_VALID) {
    rwlock(vnode, LK_EXCLUSIVE);
    bsync(vnode, BSYNC_ALL_NOW);
    rwlock(vnode, LK_RELEASE);
    vnode->flags = 0;
  }
  
  sb->reference_cnt++;
  
  InitRendez(&vnode->rendez);

  rwlock_init(&vnode->lock);
  
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
  LIST_INIT(&vnode->pendwri_buf_list);
  LIST_INIT(&vnode->delwri_buf_list);

  LIST_INIT(&vnode->dname_list);
  LIST_INIT(&vnode->directory_dname_list);

  LIST_INIT(&vnode->knote_list);
  
  return vnode;
}


/* @brief   Find an existing vnode
 */
struct VNode *vnode_get(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;

//  Info("vnode_get(sb:%08x, inode_nr:%d)", (uint32_t)sb, inode_nr);

  KASSERT(sb != NULL);

  if (sb->flags & SF_ABORT) {
    return NULL;
  }
  
  if ((vnode = vnode_find(sb, inode_nr)) != NULL) {
    vnode->reference_cnt++;
    sb->reference_cnt++;
  
    if (vnode->flags & V_FREE) {
      LIST_REM_ENTRY(&vnode_free_list, vnode, vnode_link);
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
  KASSERT(vnode != NULL);

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
//  Info("vnode_put(vnode:%08x)", (uint32_t)vnode);

  KASSERT(vnode != NULL);
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
      LIST_ADD_TAIL(&vnode_free_list, vnode, vnode_link);
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
 
  vnode_hash_remove(vnode);
  
  vnode->flags = V_FREE;
  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_link);
  vnode->reference_cnt = 0;

// sb->reference_cnt--;

  rwlock_init(&vnode->lock);

  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Find an existing vnode in the vnode cache
 *
 * TODO : Hash vnode by sb and inode_nr
 */
struct VNode *vnode_find(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;
  int h;
  
  KASSERT(sb != NULL);

  h = calc_vnode_hash(sb, inode_nr);
  vnode = LIST_HEAD(&vnode_hash[h]);
  
  while(vnode != NULL) {
    if ((vnode->flags & V_VALID) && vnode->superblock == sb && vnode->inode_nr == inode_nr) {
      return vnode;
    }
    vnode = LIST_NEXT(vnode, hash_link);
  }

  return NULL;
}


/*
 *
 */
int calc_vnode_hash(struct SuperBlock *sb, ino_t inode_nr)
{
  return inode_nr % VNODE_HASH;
}


/* @brief   Place vnode in hash table for quicker lookups
 *
 * TODO;
 */
void vnode_hash_enter(struct VNode *vnode)
{
  KASSERT(vnode != NULL);

  int h = vnode->inode_nr % VNODE_HASH;
  LIST_ADD_HEAD(&vnode_hash[h], vnode, hash_link);
}


/*
 *
 */
void vnode_hash_remove(struct VNode *vnode)
{
  KASSERT(vnode != NULL);

  int h = vnode->inode_nr % VNODE_HASH;
  
  LIST_REM_ENTRY(&vnode_hash[h], vnode, hash_link);
}




