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
  
        LIST_REM_ENTRY(&sb->pendwri_buf_list, buf, async_link);
      }


      if (buf->flags & B_DELWRI) {
        buf->flags &= ~(B_DELWRI);
        sb = buf->vnode->superblock;
  
        LIST_REM_ENTRY(&sb->delwri_buf_list, buf, async_link);
      }
      
      return buf;

    } else {
      if ((buf = LIST_HEAD(&buf_avail_list)) == NULL) {
        TaskSleep(&buf_list_rendez);
        continue;
      }

      LIST_REM_HEAD(&buf_avail_list, free_link);
      buf->flags |= B_BUSY;

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
    buf->flags &= ~(B_VALID | B_ERROR);
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
 *
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
 *
 * FIXME: Work out file size here.  Send only upto file size.
 *
 * FIXME:File size is sent with the command, so it can write less than a cluster_sz to disk
 * Or we keep track of physical block size and how many physical blocks are valid
 * in cache block.  Harder for sparse files unless we have a bitmap.
 */
int bwrite(struct Buf *buf)
{
  ssize_t xfered;
  struct VNode *vnode;
  off64_t file_offset;
  
  buf->flags = (buf->flags | B_WRITE) & ~(B_READ | B_ASYNC);
  vnode = buf->vnode;
  file_offset = buf->file_offset;

  // FIXME: Only write a partial cluster if this is last cluster
  xfered = vfs_write(vnode, KUCOPY, buf->data, PAGE_SIZE, &file_offset);

  if (xfered != PAGE_SIZE) {
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
 * This is placed on the superblock's bawrite list and will be sent
 * to the driver by the superblock's flush task.  It will also
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
  LIST_ADD_TAIL(&sb->pendwri_buf_list, buf, async_link);
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
 * A bdwrite block is released immediately so that further writes
 * can happen.
 *
 */
int bdwrite(struct Buf *buf)
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  
  vnode = buf->vnode;
  sb = vnode->superblock;

  buf->flags = (buf->flags | B_WRITE | B_DELWRI) & ~(B_READ | B_ASYNC);

  buf->expiration_time = get_hardclock() + DELWRI_DELAY_TICKS;

  LIST_ADD_TAIL(&sb->delwri_buf_list, buf, async_link);
  brelse(buf);
  
  return 0;
}


/* @brief   Write out all cached blocks of a file
 *
 * @param   vnode, file to write dirty cached blocks to disk
 * @return  0 on success, negative errno on failure.
 */
int bsync(struct VNode *vnode)
{
  // Find all blocks in cache belonging to file, immediately write out.
  // What if blocks are busy, wait?
  
  // Do we notify the bdflush_task ?
  
  return -ENOSYS;
}


/* @brief   Write out all cached blocks of a mounted filesystem.
 * 
 * @param   sb, superblock of mounted filesystem to sync contents of
 * @return  0 on success, negative errno on failure
 */
int bsyncfs(struct SuperBlock *sb)
{
  // Do we notify the bdflush_task ?

  return -ENOSYS;
}


/* @brief   Resize contents of file in cache.
 * 
 * @param   vnode, file to resize
 * @return  0 on success, negative errno on failure
 *
 * The new size must already be set within the vnode structure
 * Delete bufs if needed. Erase partial buf of last block to avoid
 * leakage of past data.  
 */
int btruncate(struct VNode *vnode)
{
  return -ENOSYS;
}


/* @brief   Dynamically change the size of the filesystem cache
 */
size_t resize_cache(size_t free)
{
  // Find out how much we need
  // while (cache_shrink_busy)
  // cond_wait();

  return -ENOSYS;
}


/*
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

  Info("bd_flush initialization OK");
  
  return 0;
}


/*
 *
 */
void fini_superblock_bdflush(struct SuperBlock *sb, int how)
{ 
  // TODO: Set how this should be shutdown, flush all or abort immediately.
  TaskWakeup(&sb->bdflush_rendez);
  
  // TODO: wait for bdflush thread to exit.
}



/* @brief   Per-Superblock kernel task for flushing async and delayed writes to disk
 *
 * TODO: Maybe have some fair share between async writes and delwri blocks?
 *
 * pending_writes   - anything that is expired is placed on this
 *                  - all async writes are placed on end of this  (do not brelse these in bawrite)
 *
 * delayed_writes   - blocks to be written out at a later time (already brelsed)
    // Either fixed rate timeout or dynamic based on next delwrite buf.
    // Maybe a minimum delay.  Handle cases if timeout is negative.
    // Initially try fixed rate.
    

 */
void bdflush_task(void *arg)
{
	struct SuperBlock *sb;
	struct Buf *buf;
  uint64_t now;
  struct timespec timeout;
  
  Info("bdflush_task started");
  
	sb = (struct SuperBlock *)arg;
	
  while((sb->flags & S_ABORT) == 0) {
  	now = get_hardclock();
  
    while ((buf = get_bdwrite_buf(sb, now)) != NULL) {      
      LIST_ADD_TAIL(&sb->pendwri_buf_list, buf, async_link);
    }
      
    while ((buf = get_pending_write_buf(sb)) != NULL) {      
      if (buf) {
            
        vfs_write(buf->vnode, KUCOPY, buf->data, PAGE_SIZE, NULL);
        brelse(buf);      
      }
    }

    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;
    
    TaskSleepInterruptible(&sb->bdflush_rendez, &timeout, INTRF_NONE);
  }
}


/*
 *
 */
struct Buf *get_bdwrite_buf(struct SuperBlock *sb, uint64_t now)
{
  struct Buf *buf;
  
  buf = LIST_HEAD(&sb->delwri_buf_list);
  
  if (buf != NULL) {
    if (buf->expiration_time <= now) {
      LIST_REM_HEAD(&sb->delwri_buf_list, async_link);
      buf->flags |= B_BUSY | B_WRITE;
      buf->flags &= ~B_DELWRI;
      return buf;
    }
  }

  return NULL;
}


/*
 *
 */
struct Buf *get_pending_write_buf(struct SuperBlock *sb)
{
  struct Buf *buf;
  
  buf = LIST_HEAD(&sb->pendwri_buf_list);
  
  if (buf != NULL) {
    LIST_REM_HEAD(&sb->pendwri_buf_list, async_link);
    buf->flags |= B_BUSY | B_WRITE;
    buf->flags &= ~(B_ASYNC | B_DELWRI);

    return buf;
  }
  
  return NULL;
}



