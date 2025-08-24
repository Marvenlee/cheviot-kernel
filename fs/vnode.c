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
#include <kernel/hash.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <string.h>
#include <kernel/kqueue.h>


/* @brief   Lookup a vnode from a file pointer
 *
 * @param   filp, file pointer object that points to the vnode
 */
struct VNode *vnode_get_from_filp(struct Filp *filp)
{
  if (filp == NULL || filp->type != FILP_TYPE_VNODE) {
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
int close_vnode(struct VNode *vnode, uint32_t filp_flags)
{   
  struct Pipe *pipe;

  Info("close_vnode(vnode:%08x)", (uint32_t)vnode);

  rwlock(&vnode->lock, LK_EXCLUSIVE);

  if (S_ISREG(vnode->mode)) {
    bsyncv(vnode);            
  } else if (S_ISFIFO(vnode->mode)) {
    pipe = vnode->pipe;
    if (filp_flags & O_RDONLY) {
      pipe->reader_cnt--;
    } else {
      pipe->writer_cnt--;
    }
  }
  
  vnode_put(vnode);             // vnode_put() does the actual cleanup when ref / link counts reach 0.
  
  // Vnode object is static, we can release its lock here
  rwlock(&vnode->lock, LK_RELEASE);
  
  return 0;
}


/* @brief   Allocate a new vnode
 *
 * Allocate a new vnode object, assign an inode_nr to it and lock it.
 * A call to vnode_get() or vnode_find() should be called prior to this to see if the vnode
 * already exists.
 *
 * If there are no vnodes available this returns NULL.
 *
 * This may block while freeing existing vnode and flushing its blocks to disk.
 */
struct VNode *vnode_new(struct SuperBlock *sb)
{
  struct VNode *vnode;

  KASSERT(sb != NULL);

//  Info("vnode_new(sb:%08x)", (uint32_t)sb);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  // FIXME: Lock vnode list as exclusive whilst we acquire vnode?
  
//  rwlock(&vnode_list_lock, LK_EXCLUSIVE);

  vnode = LIST_HEAD(&vnode_free_list);

  if (vnode == NULL) {
//    rwlock(&vnode_list_lock, LK_RELEASE);
    return NULL;
  }

  LIST_REM_HEAD(&vnode_free_list, vnode_link);

//  rwlock(&vnode_list_lock, LK_RELEASE);

//  sb->reference_cnt++;
  
//  rwlock(&vnode->lock, LK_EXCLUSIVE);

//  vnode->reference_cnt = 1;


  // Flush out any existing data and invalidate vnode blocks in cache.

  // FIXME: What if superblock of this existing vnode is being aborted (and somehow blocks?)

  // FIXME: S

  if (vnode->flags & V_VALID) {
    Info("vnode_new, bsyncv + binvalidate");
    
    KASSERT(vnode->reference_cnt == 0);    
    
    bsyncv(vnode);
    binvalidatev(vnode);
    vnode->flags = 0;
    
    LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
    deref_superblock(vnode->superblock);
  }

  vnode->inode_nr = -1;

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

  LIST_INIT(&vnode->page_list);
  
  LIST_INIT(&vnode->dname_list);
  LIST_INIT(&vnode->directory_dname_list);

  LIST_INIT(&vnode->knote_list);

//  Info("vnode->lock release");

//  rwlock(&vnode->lock, LK_RELEASE);
  
  return vnode;
}


/* @brief   Find an existing vnode
 *
 * FIXME: We don't wait for a vnode to become not busy.  Does vnode need a busy flag
 * or are we depending on rwlock instead?
 *
 * Should we be locking the vnode here?
 */
struct VNode *vnode_get(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;

  KASSERT(sb != NULL);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  rwlock(&vnode_list_lock, LK_SHARED);

  if (sb->flags & SBF_ABORT) {
    rwlock(&vnode_list_lock, LK_RELEASE);  
    return NULL;
  }
  
  if ((vnode = vnode_find(sb, inode_nr)) == NULL) {
    rwlock(&vnode_list_lock, LK_RELEASE);  
    return NULL;
  }
  
  vnode->reference_cnt++;
  sb->reference_cnt++;

  if (vnode->flags & V_FREE) {
    LIST_REM_ENTRY(&vnode_free_list, vnode, vnode_link);
  }

  rwlock(&vnode_list_lock, LK_RELEASE);  
  return vnode;
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
  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);

//  KASSERT(vnode->lock.exclusive_cnt == 1);

  // FIXME: This could cause a deadlock if vnode list is locked by the bdflush daemon and
  // it then attempts to lock this vnode which the call of vnode_put should have done.
  
  rwlock(&vnode_list_lock, LK_EXCLUSIVE);

  vnode->reference_cnt--;  
  vnode->superblock->reference_cnt--;
  
  if (vnode->reference_cnt == 0) {
    
#if 0
    if (vnode->nlink == 0) {
    // FIXME: do_vnode_discard(vnode);
    // FIXME: IF vnode->link_count == 0,  delete entries in cache.
    // FIXME: Do any special cleanup of file, dir, pipe/fifo ?
    // TODO: Mark vnode as free/invalid    
    }  
#endif

    if ((vnode->flags & V_ROOT) == 0) {      
      KASSERT(vnode->vnode_covered == NULL);
    
      vnode->flags |= V_FREE;
      LIST_ADD_TAIL(&vnode_free_list, vnode, vnode_link);
    }
  }

  rwlock(&vnode_list_lock, LK_RELEASE);
    
  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Discard a vnode, put it on the free list and mark it as invalid.
 *
 * Any bufs associated with the vnode should be discarded.
 */
void vnode_discard(struct VNode *vnode)
{
  struct SuperBlock *sb;
  
//  rwlock(&vnode_list_lock, LK_EXCLUSIVE);

  // FIXME: rwlock(&vnode->lock, LK_DRAIN);    // Do we need this for vnode_get/vnode_put ?
  
  sb = vnode->superblock;
  
  vnode_hash_remove(vnode);
  
  binvalidatev(vnode);
  
  vnode->flags = V_FREE;
  vnode->reference_cnt = 0;
  vnode->nlink = 0;

  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_link);

  deref_superblock(sb);

  // Reinitialize vnode lock.
  //  rwlock_init(&vnode->lock);

  TaskWakeupAll(&vnode->rendez);

//  rwlock(&vnode_list_lock, LK_RELEASE);

}


/* @brief   Find an existing vnode in the vnode cache
 *
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
 * TODO: Replace simple vnode hash calculation with what Minix uses (same for block cache hash)
 */
int calc_vnode_hash(struct SuperBlock *sb, ino_t inode_nr)
{
  uint32_t a = (uint32_t)sb;
  uint32_t b = (uint32_t)inode_nr;
  uint32_t c = 0xBB40E64D;

	hash_mix(a, b, c);
	hash_final(a, b, c);

  return c % VNODE_HASH;
}


/* @brief   Place vnode in hash table for quicker lookups
 *
 */
void vnode_hash_enter(struct VNode *vnode)
{
  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);

  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);
  LIST_ADD_HEAD(&vnode_hash[h], vnode, hash_link);
}


/*
 *
 */
void vnode_hash_remove(struct VNode *vnode)
{
  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);

  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);  
  LIST_REM_ENTRY(&vnode_hash[h], vnode, hash_link);
}


