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
 *
 * The filesystem system calls use rwlocks to protect vnodes with the following
 * lock types.  This follows the lock usage as described in the paper "Practical
 * Structures for Parallel Operating Systems" by Jan Edler in section 6.2.4.
 *
 * D access               shared
 * ? chdir, chroot        shared
 * y chmod, fchmod        exclusive
 * D chown, fchown        exclusive
 * ? close                shared+
 * ? directory lookup     shared
 * ? directory update     exclusive
 * y readdir              shared
 * y exec                 shared
 * x flock                exclusive  (To be implemented)
 * y fsync                shared
 * x link                 shared    (To be implemented)
 * ? open                 shared+
 * y read                 shared
 * y stat                 shared
 * y truncate, ftruncate  exclusive
 * y unlink               shared
 * x utimes               exclusive (To be implemented)
 * y write                shared+   
 * 
 * + close requires an exclusive lock when it is the last close
 * + open requires an exclusive lock when truncating a file.
 • + write requires an exclusive lock when extending a file or filling a hole.
 *   Pipes and character devices require shared access for read and write operations.
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/hash.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <string.h>


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
struct VNode *vnode_get_new(struct SuperBlock *sb)
{
  struct VNode *vnode;

  kassert(sb != NULL);

  klog_info("vnode_get_new(sb:%08x)", (uint32_t)sb);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  vnode = LIST_HEAD(&vnode_free_list);

  if (vnode == NULL) {
    klog_error("vnode_get_new() failed, no memory");
    return NULL;
  }

  LIST_REM_HEAD(&vnode_free_list, vnode_link);

  sb->reference_cnt++;
  
  if (vnode->flags & V_VALID) {
    klog_info("vnode valid, recycling");
    
    kassert(vnode->reference_cnt == 0);    
    
//    do_vnode_recycle(vnode);
  }

  vnode->reference_cnt = 1;

  vnode->superblock = sb;

  vnode->inode_nr = -1;
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

  LIST_INIT(&vnode->page_list);  
  LIST_INIT(&vnode->dname_list);
  LIST_INIT(&vnode->directory_dname_list);

  klog_info("vnode_get_new(sb:%08x) vnode:%08x, ref_cnt:%d", (uint32_t)sb, (uint32_t)vnode, vnode->reference_cnt);

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
  
  klog_info("vnode_get(sb:%08x, inode_nr:%d)", (uint32_t)sb, inode_nr);
  
  kassert(sb != NULL);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  if ((vnode = vnode_find(sb, inode_nr)) == NULL) {
    klog_info("vnode_find() failed");
    return NULL;
  }
  
  if (vnode->flags & V_FREE) {    // TODO: Rename to V_INACTIVE
    LIST_REM_ENTRY(&vnode_free_list, vnode, vnode_link);
    
    // TODO: Recycle vnode in here ? 
  }

  klog_info("vnode_get(sb:%08x, inode_nr:%d), vnode:%08x, cur ref_cnt:%d", (uint32_t)sb, inode_nr,
                                          (uint32_t)vnode, vnode->reference_cnt);
  return vnode;
}


/* @brief   Lookup a vnode from a file pointer
 *
 * @param   filp, file pointer object that points to the vnode
 */
struct VNode *vnode_get_from_filp(struct Filp *filp)
{
  if (filp == NULL) {
    klog_error("vnode_get_from_filp, filp is NULL");
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_VNODE) {
    klog_info("vnode_get_from_filp, filp->type is not vnode: %d", filp->type);
    return NULL;
  }
  
  klog_info("vnode_get_from_filp(filp:%08x) vnode:%08x, cur ref_cnt:%d", (uint32_t)filp,
                                    (uint32_t)filp->u.vnode, filp->u.vnode->reference_cnt);
  
  return filp->u.vnode;
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
  int sc;
  
  klog_info("vnode_put(vnode:%08x) prior ref_cnt: %d", (uint32_t)vnode, vnode->reference_cnt);

  return;
  
////////////////////////////////////////////////////
////////////////////////////////////////////////////
////////////////////////////////////////////////////
  

  kassert(vnode != NULL);
  kassert(vnode->superblock != NULL);
  kassert(vnode->reference_cnt > 0);

/*
  vnode->reference_cnt--;
    
  if (vnode->reference_cnt == 0) {    
    sc = vfs_close(vnode);
    
    if (sc == 0) {
      // FIle still has links and remains on disk. 
      do_vnode_inactive(vnode);
    } else {
      // Error or inode storage has been freed due to no links to file.
      do_vnode_discard(vnode);
    }

    // TODO: Check if superblock is flagged for lazy unmount.  If no more vnodes
    // Then perform the free on the superblock.
    
    TaskWakeupAll(&vnode->rendez);  
  }
*/

}


/*
 * Or should this be done in do_close_fifo() ?
 */
void vnode_put_fifo_reader(struct VNode *vnode)
{
  struct Pipe *pipe;

  kassert(S_ISFIFO(vnode->mode));
  
//  vnode->reference_cnt--;
    
  pipe = vnode->pipe;
  pipe->reader_cnt--;
  
  klog_info("vnode_put_fifo_reader reader_cnt:%d, writer_cnt:%d", pipe->reader_cnt, pipe->writer_cnt);
  
  if (pipe->reader_cnt == 0) {
    TaskWakeupAll(&pipe->rendez);
  }
  
  if (pipe->reader_cnt == 0 && pipe->writer_cnt == 0) {
    kassert(vnode->reference_cnt == 0);    
  //  do_vnode_discard(vnode);
  }
}


/*
 *
 */
void vnode_put_fifo_writer(struct VNode *vnode)
{
  struct Pipe *pipe;
  
  kassert(S_ISFIFO(vnode->mode));
  

  // vnode->reference_cnt--;
  
  pipe = vnode->pipe;
  pipe->writer_cnt--;

  klog_info("vnode_put_fifo_writer reader_cnt:%d, writer_cnt:%d", pipe->reader_cnt, pipe->writer_cnt);

  if (pipe->writer_cnt == 0) {
    TaskWakeupAll(&pipe->rendez);
  }
  
  if (pipe->reader_cnt == 0 && pipe->writer_cnt == 0) {
    kassert(vnode->reference_cnt == 0);    
    do_vnode_discard(vnode);
  }
}


/* @brief   Increment vnode reference count
 *
 */
void vnode_ref(struct VNode *vnode)
{
  klog_info("vnode_ref(vnode:%08x) ref_cnt: %d", (uint32_t)vnode, vnode->reference_cnt);
  vnode->reference_cnt++;
}


/* @brief   Discard a vnode, put it on the free list and mark it as invalid.
 *
 * Any bufs associated with the vnode should be discarded.
 */
void do_vnode_discard(struct VNode *vnode)
{
//  struct Pipe *pipe; FIXME: Do we need to clean up pipe?

  klog_info("do_vnode_discard(vnode:%08x)", (uint32_t)vnode);


  return;
  
////////////////////////////////////////////////////
////////////////////////////////////////////////////
////////////////////////////////////////////////////



  kassert(vnode->reference_cnt == 0);

//  rwlock_drain(&vnode->lock);
  
  if (S_ISREG(vnode->mode)) {
     btruncatev(vnode);
     bsyncv(vnode);
  } else if (S_ISDIR(vnode->mode)) {
     btruncatev(vnode);
     bsyncv(vnode);
  } else if (S_ISFIFO(vnode->mode)) {
    free_pipe(vnode->pipe);
    vnode->pipe = NULL;
  }
  

  vnode_hash_remove(vnode);

  vnode->flags = V_FREE;
  vnode->reference_cnt = 0;

  vnode->superblock->reference_cnt--;

  LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_link);

//  rwlock_reset(&vnode->lock);

  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Put vnode on end of free list so that it remains cached.
 *
 */
void do_vnode_inactive(struct VNode *vnode)
{
//  struct Pipe *pipe;    FIXME: Do we need to handle pipe

  klog_info("do_vnode_inactive(vnode:%08x)", (uint32_t)vnode);

  return;
  
////////////////////////////////////////////////////
////////////////////////////////////////////////////
////////////////////////////////////////////////////

  kassert(vnode->reference_cnt == 0);

  klog_info("do_vnode_inactive(vnode:%08x)", (uint32_t)vnode);

//  rwlock_drain(&vnode->lock);
  
  if (S_ISREG(vnode->mode)) {
     bsyncv(vnode);
  } else if (S_ISDIR(vnode->mode)) {
     bsyncv(vnode);
  } else if (S_ISFIFO(vnode->mode)) {
    free_pipe(vnode->pipe);
    vnode->pipe = NULL;
  }
  
  vnode->flags = V_FREE;

  LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_link);

//  rwlock_reset(&vnode->lock);

  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Recycle a vnode
 *
 * Any bufs associated with the vnode should be discarded.
 */
void do_vnode_recycle(struct VNode *vnode)
{
  struct SuperBlock *sb;

  klog_info("do_vnode_recycle(vnode:%08x)", (uint32_t)vnode);
  
  return;

////////////////////////////////////////////////////
////////////////////////////////////////////////////
////////////////////////////////////////////////////


  // May need exlusive lock on vnode
  rwlock_drain(&vnode->lock);
  
  if (vnode->reference_cnt > 0) {
    return;
  }
  
  sb = vnode->superblock;
    
  if (S_ISREG(vnode->mode)) {
    bsyncv(vnode);
    binvalidatev(vnode);
  } else if (S_ISFIFO(vnode->mode)) {
    if (vnode->pipe != NULL) {
      free_pipe(vnode->pipe);
      vnode->pipe = NULL;
    }
  }

  vnode_hash_remove(vnode);

  vnode->flags = V_FREE;
  vnode->reference_cnt = 0;

  LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
  LIST_ADD_HEAD(&vnode_free_list, vnode, vnode_link);

  sb->reference_cnt--;

  rwlock_reset(&vnode->lock);

  TaskWakeupAll(&vnode->rendez);
}


/* @brief   Find an existing vnode in the vnode cache
 *
 */
struct VNode *vnode_find(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;
  int h;
  
  klog_info("vnode_find(sb:%08x, inode_nr:%d)", (uint32_t)sb, inode_nr);
  
  kassert(sb != NULL);

  h = calc_vnode_hash(sb, inode_nr);
  vnode = LIST_HEAD(&vnode_hash[h]);
  
  while(vnode != NULL) {
    if ((vnode->flags & V_VALID) && vnode->superblock == sb && vnode->inode_nr == inode_nr) {
      klog_info("vnode found: %08x", (uint32_t)vnode);
      return vnode;
    }
    vnode = LIST_NEXT(vnode, hash_link);
  }

  klog_info("vnode not found");
  return NULL;
}


/* @brief   Calculate hash value of vnode for vnode lookup table
 *
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
  kassert(vnode != NULL);
  kassert(vnode->superblock != NULL);
  kassert((vnode->flags & V_HASHED) == 0);
  
  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);
  LIST_ADD_HEAD(&vnode_hash[h], vnode, hash_link);

  vnode->flags |= V_HASHED;
}


/*
 *
 */
void vnode_hash_remove(struct VNode *vnode)
{
  kassert(vnode != NULL);
  kassert(vnode->superblock != NULL);
  kassert(vnode->flags & V_HASHED);

  vnode->flags &= ~V_HASHED;

  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);  
  LIST_REM_ENTRY(&vnode_hash[h], vnode, hash_link);
}


