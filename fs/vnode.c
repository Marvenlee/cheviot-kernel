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

  KASSERT(sb != NULL);

  Info("vnode_get_new(sb:%08x)", (uint32_t)sb);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  vnode = LIST_HEAD(&vnode_free_list);

  if (vnode == NULL) {
    return NULL;
  }

  LIST_REM_HEAD(&vnode_free_list, vnode_link);

  sb->reference_cnt++;
  
  if (vnode->flags & V_VALID) {
    Info("vnode valid, recycling");
    
    KASSERT(vnode->reference_cnt == 0);    
    
    do_vnode_recycle(vnode);
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

  Info("vnode_get(sb:%08x, inode_nr:%d)", (uint32_t)sb, inode_nr);
  
  KASSERT(sb != NULL);

  if (sb->flags & SBF_ABORT) {
    return NULL;
  }

  if ((vnode = vnode_find(sb, inode_nr)) == NULL) {
    return NULL;
  }
  
  if (vnode->flags & V_FREE) {    // TODO: Rename to V_INACTIVE
    LIST_REM_ENTRY(&vnode_free_list, vnode, vnode_link);
    
    // TODO: Recycle vnode in here ?
    
  }

  Info("vnode_get() gotten vnode:%08x", (uint32_t)vnode);
  return vnode;
  
  
}


/* @brief   Lookup a vnode from a file pointer
 *
 * @param   filp, file pointer object that points to the vnode
 */
struct VNode *vnode_get_from_filp(struct Filp *filp)
{
  Info("vnode_get_From_filp()");
  
  if (filp == NULL) {
    Error("vnode_get_from_filp, filp is NULL");
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_VNODE) {
    Info("vnode_get_from_filp, filp->type is not vnode: %d", filp->type);
    return NULL;
  }
  
  Info("..vnode_get_from_filp(filp:%08x) vnode:%08x", (uint32_t)filp, (uint32_t)filp->u.vnode);
  
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
  Info("vnode_put(vnode:%08x)", (uint32_t)vnode);

  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);

//  KASSERT(S_ISFIFO(vnode->mode) == 0);
//  KASSERT(vnode->reference_cnt > 0);
  

  if (S_ISREG(vnode->mode)) {
    // bsyncv(vnode);
  } else if (S_ISDIR(vnode->mode)) {
    
    //knote(&dvnode->knote_list, NOTE_WRITE | NOTE_ATTRIB);  
  }


  vnode->reference_cnt--;
    
  if (vnode->reference_cnt == 0) {    
    if (vnode->nlink == 0) {
//      vnode_discard(vnode);    
    }
    
    //TaskWakeupAll(&vnode->rendez);
  }


}


/*
 * Or should this be done in do_close_fifo() ?
 */
void vnode_put_fifo_reader(struct VNode *vnode)
{
  struct Pipe *pipe;

  KASSERT(S_ISFIFO(vnode->mode));
  
  vnode->reference_cnt--;
    
  pipe = vnode->pipe;
  pipe->reader_cnt--;
  
  Info("vnode_put_fifo_reader reader_cnt:%d, writer_cnt:%d", pipe->reader_cnt, pipe->writer_cnt);
  
  if (pipe->reader_cnt == 0) {
    TaskWakeupAll(&pipe->rendez);
  }
  
  if (pipe->reader_cnt == 0 && pipe->writer_cnt == 0) {
    KASSERT(vnode->reference_cnt == 0);    
    do_vnode_discard(vnode);
  }
}


/*
 *
 */
void vnode_put_fifo_writer(struct VNode *vnode)
{
  struct Pipe *pipe;
  
  KASSERT(S_ISFIFO(vnode->mode));
  

  vnode->reference_cnt--;
  
  pipe = vnode->pipe;
  pipe->writer_cnt--;

  Info("vnode_put_fifo_writer reader_cnt:%d, writer_cnt:%d", pipe->reader_cnt, pipe->writer_cnt);

  if (pipe->writer_cnt == 0) {
    TaskWakeupAll(&pipe->rendez);
  }
  
  if (pipe->reader_cnt == 0 && pipe->writer_cnt == 0) {
    KASSERT(vnode->reference_cnt == 0);    
    do_vnode_discard(vnode);
  }
}


/*
 *
 */
void vnode_ref(struct VNode *vnode)
{
  vnode->reference_cnt++;
}




/* @brief   Discard a vnode, put it on the free list and mark it as invalid.
 *
 * Any bufs associated with the vnode should be discarded.
 */
void do_vnode_discard(struct VNode *vnode)
{
  struct Pipe *pipe;

  Info("vnode_discard(vnode:%08x)", (uint32_t)vnode);

  if (S_ISREG(vnode->mode)) {
    Info("close_vnode reg file");
//    bsyncv(vnode);
  } else if (S_ISFIFO(vnode->mode)) {
    Info("close_vnode pipe");
    
    pipe = vnode->pipe;

    Info(".. reader_cnt:%d, writer_cnt:%d", pipe->reader_cnt, pipe->writer_cnt);

    free_pipe(pipe);
  }
  
  vnode->flags = V_FREE;
  vnode->reference_cnt = 0;
  vnode->nlink = 0;

  LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
  LIST_ADD_TAIL(&vnode_free_list, vnode, vnode_link);
}


/* @brief   Recycle a vnode
 *
 * Any bufs associated with the vnode should be discarded.
 */
void do_vnode_recycle(struct VNode *vnode)
{
  struct SuperBlock *sb;

  Info("************ vnode_recycle(vnode:%08x) **********", (uint32_t)vnode);  
  
  if (vnode->reference_cnt > 0) {
    return;
  }
  
  sb = vnode->superblock;
  
  vnode_hash_remove(vnode);
  
  if (S_ISREG(vnode->mode)) {
    bsyncv(vnode);
    binvalidatev(vnode);
  } else if (S_ISFIFO(vnode->mode)) {
    // FIXME: Do any special cleanup of file, dir, pipe/fifo ?
  }

  vnode->flags = V_FREE;
  vnode->reference_cnt = 0;
  vnode->nlink = 0;

  LIST_REM_ENTRY(&vnode->superblock->vnode_list, vnode, vnode_link);  
  LIST_ADD_TAIL(&vnode_free_list, vnode, vnode_link);


  sb->reference_cnt--;

//  TaskWakeupAll(&vnode->rendez);
}




/* @brief   Find an existing vnode in the vnode cache
 *
 */
struct VNode *vnode_find(struct SuperBlock *sb, int inode_nr)
{
  struct VNode *vnode;
  int h;
  
  Info("vnode_find(sb:%08x, inode_nr:%d)", (uint32_t)sb, inode_nr);
  
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
  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);
  KASSERT((vnode->flags & V_HASHED) == 0);
  
  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);
  LIST_ADD_HEAD(&vnode_hash[h], vnode, hash_link);

  vnode->flags |= V_HASHED;
}


/*
 *
 */
void vnode_hash_remove(struct VNode *vnode)
{
  KASSERT(vnode != NULL);
  KASSERT(vnode->superblock != NULL);
  KASSERT(vnode->flags & V_HASHED);

  vnode->flags &= ~V_HASHED;

  int h = calc_vnode_hash(vnode->superblock, vnode->inode_nr);  
  LIST_REM_ENTRY(&vnode_hash[h], vnode, hash_link);
}


