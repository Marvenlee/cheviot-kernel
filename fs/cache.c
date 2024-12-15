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
#include <kernel/timer.h>
#include <kernel/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>


/* @brief   Read from a file through the VFS file cache
 *
 * @param   vnode, file to read from
 * @param   dst, destination address to copy file data to (kernel or user)
 * @param   sz, number of bytes to read
 * @param   offset, pointer to filp's offset which will be updated
 * @param   inkernel, set to true if the destination address is in the kernel (for kread)
 * @return  number of bytes read or negative errno on failure  
 */
ssize_t read_from_cache(struct VNode *vnode, void *dst, size_t sz, off64_t *offset, bool inkernel)
{
  struct Buf *buf;
  off64_t cluster_base;
  off64_t cluster_offset;
  size_t nbytes_xfer;
  size_t nbytes_total;
  size_t remaining_in_file;
  size_t nbytes_to_read;
  size_t remaining_to_xfer;  
  size_t remaining_in_cluster;    

  if (*offset >= vnode->size) {
    return 0;
  }

  if (*offset >= vnode->size) {
    return 0;
  }

  remaining_in_file = vnode->size - *offset;

  nbytes_total = 0;
  nbytes_to_read = (remaining_in_file < sz) ? remaining_in_file : sz;

  while (nbytes_total < nbytes_to_read) {  
    cluster_base = ALIGN_DOWN(*offset, PAGE_SIZE);
    cluster_offset = *offset % PAGE_SIZE;

    remaining_to_xfer = nbytes_to_read - nbytes_total;
    remaining_in_cluster = PAGE_SIZE - cluster_offset;
    nbytes_xfer = (remaining_to_xfer < remaining_in_cluster) ? remaining_to_xfer : remaining_in_cluster;
		
    buf = bread(vnode, cluster_base);

    if (buf == NULL) {
      Warn("bread buf is NULL");
      
      if (nbytes_total > 0) {
        return nbytes_total;
      } else {
        return -EIO;
      }
    }

    if (inkernel == true) {
      memcpy(dst, buf->data + cluster_offset, nbytes_xfer);
    } else {    
      if (CopyOut(dst, buf->data + cluster_offset, nbytes_xfer) != 0) {
      	return -EFAULT;
      }
    }
    
    brelse(buf);

    dst += nbytes_xfer;
    *offset += nbytes_xfer;
    nbytes_total += nbytes_xfer;
  }

  return nbytes_total;
}


/* @brief   Write to a file through the VFS file cache
 *
 * @param   vnode, file to write to
 * @param   src, user-space source address of the data to be written to the file
 * @param   sz, number of bytes to write
 * @param   offset, pointer to filp's offset which will be updated
 * @return  number of bytes written or negative errno on failure  
 *
 * If we are writing a full block, can we avoid reading it in?
 * if block doesn't exist, does bread create it?
 *
 * FIXME: Do we need an exclusive lock?
 */
ssize_t write_to_cache(struct VNode *vnode, void *src, size_t sz, off64_t *offset)
{
  struct Buf *buf;
  off_t cluster_base;
  off_t cluster_offset;
  size_t nbytes_xfer;
  size_t nbytes_total;
  size_t nbytes_to_write;
  size_t remaining_to_xfer;  
  size_t remaining_in_cluster;  

	nbytes_total = 0;
  nbytes_to_write = sz;

  while (nbytes_total < nbytes_to_write) {  
    cluster_base = ALIGN_DOWN(*offset, PAGE_SIZE);
    cluster_offset = *offset % PAGE_SIZE;

		remaining_to_xfer = nbytes_to_write - nbytes_total;
		remaining_in_cluster = PAGE_SIZE - cluster_offset;
		nbytes_xfer = (remaining_to_xfer < remaining_in_cluster) ? remaining_to_xfer : remaining_in_cluster;
		
		if (cluster_base < vnode->size) {
      buf = bread(vnode, cluster_base);
    } else {
      buf = bread_zero(vnode, cluster_base);
    }
    
    if (buf == NULL) {
      if (nbytes_total > 0) {
        return nbytes_total;
      } else {
        return -EIO;
      }
    }

    if (CopyIn(buf->data + cluster_offset, src, nbytes_xfer) != 0) {
      return -EFAULT;  
    }
		 
    src += nbytes_xfer;
    *offset += nbytes_xfer;
    nbytes_total += nbytes_xfer;

    // Update file size if we have written past the end of file    
    if (*offset > vnode->size) {
      vnode->size = *offset;
    }

    if ((*offset % PAGE_SIZE) == 0) {
      bawrite(buf);
    } else {
      bdwrite(buf);
    }    
  }

  return nbytes_total;
}


/* @Brief   Get a cached block
 *
 * @param   vnode, file to get cached block of
 * @param   file_offset, offset within file to read (aligned to page size)
 * @return  buf on success, NULL if it cannot find or create a buf
 *
 * This cache operates at the file level and not the block level. As such it
 * has no understanding of the on disk formatting of a file system. User space
 * servers implementing file system handlers are free to implement block level
 * caching if needed.
 *
 * See Maurice Bach's 'The Design of the UNIX Operating System' for notes on
 * getblk, bread, bwrite, bawrite and brelse operations (though they applied to
 * the block level in the book).
 *
 * If cache is now single page blocks instead of larger 16k or 64k clusters
 * can we use the phys mem mapping to access pages?
 * Do we need to map cached blocks into a certain area of the kernel?
 *
 * Do we need to write the entire cluster?  Is this handled by strategy params?
 *
 * TODO: Need better hash value than use file offset otherwise with lots of small
 * files the lower hash table buckets will be highly populated and the higher
 * hash table buckets will be almost empty.
 */
struct Buf *getblk(struct VNode *vnode, uint64_t file_offset)
{
  struct Buf *buf;
  struct Pageframe *pf;
  struct SuperBlock *sb;
  vm_addr pa;
  int h;

  while (1) {
    if ((buf = findblk(vnode, file_offset)) != NULL) {
      if (buf->flags & B_BUSY) {
        TaskSleep(&buf->rendez);
        continue;
      }

      buf->flags |= B_BUSY;

      if (buf->flags & B_ASYNC) {
        buf->flags &= ~(B_ASYNC);
        sb = buf->vnode->superblock;
  
        LIST_REM_ENTRY(&vnode->pendwri_buf_list, buf, async_link);
      }


      if (buf->flags & B_DELWRI) {
        buf->flags &= ~(B_DELWRI);
        sb = buf->vnode->superblock;
  
        LIST_REM_ENTRY(&vnode->delwri_buf_list, buf, async_link);
      }
      
      return buf;

    } else {
      if ((buf = LIST_HEAD(&buf_avail_list)) == NULL) {
        TaskSleep(&buf_list_rendez);
        continue;
      }

      LIST_REM_HEAD(&buf_avail_list, free_link);
      buf->flags |= B_BUSY;

      // Buf is valid, not dirty
      if (buf->flags & B_VALID) {
        h = calc_buf_hash(buf->vnode->inode_nr, buf->file_offset);
        LIST_REM_ENTRY(&buf_hash[h], buf, hash_link);
      }

      buf->flags &= ~B_VALID;
      buf->vnode = vnode;
      buf->file_offset = file_offset;
      
      h = calc_buf_hash(vnode->inode_nr, file_offset);
      LIST_ADD_HEAD(&buf_hash[h], buf, hash_link);

      return buf;
    }
  }
}


/* @brief   Release a cache block
 *
 * @param   buf, buf to be be released
 */
void brelse(struct Buf *buf)
{
  int h;
  
  if (buf->flags & (B_ERROR | B_DISCARD)) {
    h = calc_buf_hash(buf->vnode->inode_nr, buf->file_offset);
  
    LIST_REM_ENTRY(&buf_hash[h], buf, hash_link);
    buf->flags = 0;
    buf->file_offset = 0;
    buf->vnode = NULL;

    if (buf->data != NULL) {
      LIST_ADD_HEAD(&buf_avail_list, buf, free_link);
    }
  } else if (buf->data != NULL) {
    LIST_ADD_TAIL(&buf_avail_list, buf, free_link);
  }

  buf->flags &= ~B_BUSY;
  TaskWakeupAll(&buf_list_rendez);
  TaskWakeupAll(&buf->rendez);
}


/* @brief   Find a specific file's block in the cache
 *
 * @param   vnode, file to find block of
 * @param   file_offset, offset within the file (aligned to page size)
 * @return  buf on success, null if not present
 */
struct Buf *findblk(struct VNode *vnode, uint64_t file_offset)
{
  struct Buf *buf;
  int h;
  
  h = calc_buf_hash(vnode->inode_nr, file_offset);
  buf = LIST_HEAD(&buf_hash[h]);

  while (buf != NULL) {
    if (buf->vnode == vnode && buf->file_offset == file_offset)
      return buf;

    buf = LIST_NEXT(buf, hash_link);
  }

  return NULL;
}


/*
 *
 */
int calc_buf_hash(ino_t inode_nr, off64_t file_offset)
{
  return (inode_nr + (file_offset / PAGE_SIZE)) % BUF_HASH;
}


/* @brief   Read a block from a file
 *
 * @param   vnode, vnode of file to read
 * @param   file_offset, reads a block from disk into the cache (aligned to cluster size)
 * @return  Pointer to a Buf that represents a cached block or NULL on failure
 *
 * This searches for the block in the cache. if a block is not present
 * then a new block is allocated in the cache and if needed it's contents
 * read from disk.
 *
 * In the current implementation the cache's block size is 4 Kb.
 * A block read will be of this size. If the block is at the end of the
 * file the remaining bytes after the end will be zero.
 */
struct Buf *bread(struct VNode *vnode, off64_t file_offset)
{
  struct Buf *buf;
  ssize_t xfered;

  buf = getblk(vnode, file_offset);

  if (buf->flags & B_VALID) {
    return buf;
  }

  buf->flags = (buf->flags | B_READ) & ~(B_WRITE | B_ASYNC);

  xfered = vfs_read(vnode, KUCOPY, buf->data, PAGE_SIZE, &file_offset);

  if (xfered > PAGE_SIZE) {
    Error("bread > PAGE_SIZE: %d", xfered);
  }

	if (xfered <= 0) {
		Error("bread error: %d", xfered);
		buf->flags |= B_ERROR;
	} else if (xfered != PAGE_SIZE) {
		memset(buf->data + xfered, 0, PAGE_SIZE - xfered);
  }
  
  if (buf->flags & B_ERROR) {
    Error("block read failed");
    brelse(buf);
    return NULL;
  }

  buf->flags = (buf->flags | B_VALID) & ~B_READ;
  return buf;
}


/*
 *
 */
struct Buf *bread_zero(struct VNode *vnode, off64_t file_offset)
{
  struct Buf *buf;

  buf = getblk(vnode, file_offset);

  if (buf->flags & B_VALID) {
    Warn("bzero ok (cached) ???");
    return buf;
  }

  memset(buf->data, 0, PAGE_SIZE);

  buf->flags = (buf->flags | B_VALID) & ~B_READ;

  return buf;
}


/* @brief   Writes a block to disk and releases it. Waits for IO to complete.
 * 
 * @param   buf, buffer to write
 * @return  0 on success, negative errno on failure
 */
int bwrite(struct Buf *buf)
{
  ssize_t xfered;
  struct VNode *vnode;
  off64_t file_offset;
  off_t nbytes_to_write;
  
  buf->flags = (buf->flags | B_WRITE) & ~(B_READ | B_ASYNC);
  vnode = buf->vnode;
  file_offset = buf->file_offset;

  if ((vnode->size - buf->file_offset) < PAGE_SIZE) {
    nbytes_to_write = vnode->size % PAGE_SIZE;
  } else {
    nbytes_to_write = PAGE_SIZE;
  }

  xfered = vfs_write(vnode, KUCOPY, buf->data, nbytes_to_write, &file_offset);

  if (xfered != nbytes_to_write) {
    buf->flags |= B_ERROR;
  }
  
  if (buf->flags & B_ERROR) {
    brelse(buf);
    return -1;
  }

  buf->flags &= ~B_WRITE;
  brelse(buf);
  return 0;
}


/* @brief   Write buffer asynchronously.
 *
 * @param   buf, buffer to write
 * @return  0 on success, negative errno on failure
 *
 * Queue buffer for write. This block is unlikely to be
 * written again anytime soon so write as soon as possible.
 *
 * This is called when a write will reaches the end of a block
 * with the expectation that further writes will be in another block.
 *
 * This is placed on the superblock's pending write list and will be sent
 * to the driver by the superblock's bdflush task.  It will also
 * be released by the sb_flush task.
 */
int bawrite(struct Buf *buf)
{
  struct SuperBlock *sb;
  struct VNode *vnode;

  vnode = buf->vnode;
  sb = vnode->superblock;
    
  buf->flags = (buf->flags | B_WRITE | B_ASYNC) & ~(B_READ | B_DELWRI);

  buf->expiration_time = get_hardclock();  
  LIST_ADD_TAIL(&vnode->pendwri_buf_list, buf, async_link);
  TaskWakeup(&sb->bdflush_rendez);
  return 0;
}


/* @brief   Delayed write to buffer
 *
 * @param   buf, buffer to write
 * @return  0 on success, negative errno on failure
 *
 * Delay writing of a block for a few seconds. This block is likely
 * to be written again soon so schedule it to be written out after a
 * short delay.
 *
 * A bdwrite block is marked as dirty but is released immediately so
 * that further writes can happen.  The block is placed on the vnode's
 * list of dirty buffers. This list is periodically scanned by the
 * superblock's bdflush task and written to disk.
 */
int bdwrite(struct Buf *buf)
{
  struct VNode *vnode;
  
  vnode = buf->vnode;

  buf->flags = (buf->flags | B_WRITE | B_DELWRI) & ~(B_READ | B_ASYNC);

  buf->expiration_time = get_hardclock() + DELWRI_DELAY_TICKS;

  LIST_ADD_TAIL(&vnode->delwri_buf_list, buf, async_link);
  brelse(buf);
  
  return 0;
}


/* @brief   Resize contents of file in cache.
 * 
 * @param   vnode, file to resize
 * @return  0 on success, negative errno on failure
 *
 * Removes any buffers in the cache whose offset is beyond the current file size.
 * If the end of the file is partially within a buffer then the buffer is kept but
 * the remainder of the buffer is wiped clean.
 * 
 * The new size must already be set within the vnode structur and the vnode
 * should already have an exclusive lock.
 */
int btruncate(struct VNode *vnode)
{
  struct Buf *buf;
  struct Buf *next;
  
  Info("btruncate() inode_nr:%u, size:%u", (uint32_t)vnode->inode_nr, (uint32_t)vnode->size);
  
  buf = LIST_HEAD(&vnode->buf_list);
  
  while(buf != NULL) {
    // vnode is locked, does this mean all bufs are NOT BUSY ?
    // FIXME: do we need to take_buf() so that we have a lock on it ?????
  
    next = LIST_NEXT(buf, vnode_link); 
  
    if (vnode->size <= buf->file_offset) {
      // Remove buf from file, mark as free
      bdiscard(buf);
    } else if ((vnode->size - buf->file_offset) < PAGE_SIZE) {
      // Clear partial buf at end of file, mark as dirty
      off_t cluster_offset = vnode->size - buf->file_offset;
      off_t remaining = PAGE_SIZE - cluster_offset;
      
      memset(buf->data + cluster_offset, 0, remaining);
      bawrite(buf);
    }
      
    buf = next;
  }  

  return 0;
}


/* @brief   Discard a buffer in the cache, removing it from a vnode
 *
 * @param   buf, buffer to discard
 * @return  0 on success, negative errno on failure
 *
 * TODO: Remove buffer from any vnode lists it is on.
 */
int bdiscard(struct Buf *buf)
{
  buf->flags |= B_DISCARD;
  brelse(buf);
  return 0;
}
 

/* @brief   Write out all cached blocks of a file
 *
 * @param   vnode, file to write dirty cached blocks to disk
 * @param   now, write out blocks that have an expiry time less than 'now' ticks
 * @return  0 on success, negative errno on failure.
 *
 * Find all dirty blocks in cache belonging to file, immediately write out.
 *
 * The vnode must be exclusive locked prior to performing bsync
 * so that the dirty buf list cannot be modified by other tasks.
 * 
 * There are 2 queues:
 *
 * delayed_writes   - blocks to be written out at a later time. These are
 *                    blocks written with bdwrite and are expected to be
 *                    written again soon. Blocks can be removed from this
 *                    and written again.
 *                    bsync() removes a number of blocks from this list
 *                    that have been dirty for several seconds and places
 *                    them on the pending_writes list.
 *
 * pending_writes   - buffers queued for writing out to disk. Blocks cannot
 *                    be removed from this list. All async bawrite() buffers 
 *                    are placed immediately at the end of this list. Delayed
 *                    bdwrite() buffers are placed on this list by the
 *                    call to bsync().
 *
 * All bawrite blocks are written out as they will all have expired.  A
 * number of dirty blocks from bdwrite() that have not been written for
 * a few seconds will be written out and marked as not-dirty.
 *
 * TODO: We could do some processing in here to sort blocks and write out
 * larger runs of blocks as a multipart IOV message.
 * 
 * If a block is on the dirty buffer list then by definition it is not busy
 */
int bsync(struct VNode *vnode, uint64_t now)
{
  int saved_sc = 0;
  struct Buf *buf;
  off_t nbytes_to_write;
      
  while((buf = bgetdirtybuf(vnode, now)) != NULL) {
    LIST_ADD_TAIL(&vnode->pendwri_buf_list, buf, async_link);

    buf->flags &= ~B_DELWRI;  
    buf->flags |= B_BUSY | B_ASYNC | B_WRITE;
  }
    
  while ((buf = LIST_HEAD(&vnode->pendwri_buf_list)) != NULL) {  
    LIST_REM_HEAD(&vnode->pendwri_buf_list, async_link);
    if ((vnode->size - buf->file_offset) < PAGE_SIZE) {
      nbytes_to_write = vnode->size % PAGE_SIZE;
    } else {
      nbytes_to_write = PAGE_SIZE;
    }

    int sc = vfs_write(vnode, KUCOPY, buf->data, nbytes_to_write, NULL);

    if (sc != 0 && saved_sc == 0) {
      saved_sc = sc;
    }

    brelse(buf);
  }
    
  return saved_sc;
}


/* @brief   Write out all cached blocks of a mounted filesystem.
 * 
 * @param   sb, superblock of mounted filesystem to sync contents of
 * @param   now, write out blocks that have an expiry time less than 'now' ticks
 * @return  0 on success, negative errno on failure
 *
 * Goes through the superblock's list of vnodes and writes out dirty
 * blocks of each vnode tha .
 */
int bsyncfs(struct SuperBlock *sb, uint64_t now)
{
  int saved_sc = 0;  
  struct VNode *vnode;
  
  rwlock(&sb->lock, LK_EXCLUSIVE);             //For locking vnode mount list
                                                // But does it also protect vnode->buf_list ?
  vnode = LIST_HEAD(&sb->vnode_list);
  
  while (vnode != NULL) {
    rwlock(&vnode->lock, LK_EXCLUSIVE);
    int sc = bsync(vnode, now);
    rwlock(&vnode->lock, LK_RELEASE);
    
    if (sc != 0 && saved_sc == 0) {
      saved_sc = sc;
    }
    
    vnode = LIST_NEXT(vnode, vnode_link);
  }
  
  rwlock(&sb->lock, LK_RELEASE);

  TaskWakeupAll(&sb->bdflush_rendez);
  
  return saved_sc;
}


/* @brief   Per-Superblock kernel task for flushing async and delayed writes to disk
 *
 * @param   arg, pointer to the superblock
 */
void bdflush_task(void *arg)
{
	struct SuperBlock *sb;
  uint64_t now;
  struct timespec timeout;
  
	sb = (struct SuperBlock *)arg;
	
  while((sb->flags & SF_ABORT) == 0) {
  	now = get_hardclock();  	
    bsyncfs(sb, now);

    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;
    TaskSleepInterruptible(&sb->bdflush_rendez, &timeout, INTRF_NONE);
  }  
}


/* @brief   Get a block marked as dirty for the current vnode.
 *
 * @param   vnode, vnode to get a dirty block from
 * @param   now, get a block with an expiry time less than 'now' ticks
 * @return  A buffer that is dirty or NULL if no buffers due to expire
 */
struct Buf *bgetdirtybuf(struct VNode *vnode, uint64_t now)
{
  struct Buf *buf;
  
  buf = LIST_HEAD(&vnode->delwri_buf_list);
  
  if (buf != NULL) {
    if (buf->expiration_time <= now) {
      LIST_REM_HEAD(&vnode->delwri_buf_list, async_link);
            
      buf->flags |= B_BUSY | B_WRITE;
      buf->flags &= ~B_DELWRI;
      return buf;
    }
  }

  return NULL;
}


/* @brief   Create a kernel task to periodically flush a filesystem's dirty blocks
 * 
 */
int init_superblock_bdflush(struct SuperBlock *sb)
{
  Info("init_superblock_bdflush");
  
  sb->bdflush_thread = create_kernel_thread(bdflush_task, sb, 
                                        SCHED_RR, SCHED_PRIO_CACHE_HANDLER, 
                                        THREADF_KERNEL, NULL, "bdflush-kt");
  
  if (sb->bdflush_thread == NULL) {
    Info("bd_flush initialization failed");
    return -ENOMEM;
  }
  
  return 0;
}


/* @brief   Shutdown the bdflush kernel task of a filesystem
 *
 * @param   sb, superblock of the bdflush task to stop
 * @param   how, option to control how the task is stopped
 *
 * TODO: Set how this should be shutdown, flush all or abort immediately.
 */
void fini_superblock_bdflush(struct SuperBlock *sb, int how)
{ 
  sb->flags |= SF_ABORT;
  TaskWakeup(&sb->bdflush_rendez);

  if (sb->bdflush_thread != NULL) {
    do_join_thread(sb->bdflush_thread, NULL);
  }
  
  sb->bdflush_thread = NULL;
}


