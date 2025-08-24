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
#include <kernel/hash.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>


/* @Brief   Get a cached block
 *
 * @param   vnode, file to get cached block of
 * @param   file_offset, offset within file to read (aligned to page size)
 * @return  Page on success, NULL if it cannot find or allocate a Page
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
 * TODO: Add timeout for getting a block
 */
struct Page *getblk(struct VNode *vnode, uint64_t file_offset)
{
  struct Page *page;

  Info("getblk(vnode:%08x, offs:%08x)", (uint32_t)vnode, (uint32_t)file_offset);

  while (1) {
    if (vnode->superblock->flags & SBF_ABORT) {
      return NULL;
    }
  
    if ((page = find_blk(vnode, file_offset)) != NULL) {
      if (page->bflags & B_BUSY) {
        TaskSleep(&page->rendez);
        continue;
      }

      page->bflags |= B_BUSY;
      
      remove_from_free_page_queue(page);

      if ((page->bflags & B_VALID) == 0) {
        memset(page->vaddr, 0, PAGE_SIZE);
      }

      return page;

    } else {
      if ((page = find_available_blk()) == NULL) {
        Info("find_available_blk, none found, sleeping");
        TaskSleep(&page_list_rendez);
        continue;
      }

      KASSERT((page->bflags & B_BUSY) == 0);

      page->bflags |= B_BUSY;

      remove_from_free_page_queue(page);
      
      if (page->bflags & B_VALID) {
        remove_from_lookup_page_hash(page);
        remove_from_vnode_page_list(page);

        page->bflags &= ~B_VALID;
      }

      page->vnode = vnode;      
      page->file_offset = file_offset;
      
      add_to_lookup_page_hash(page);
      add_to_vnode_page_list(page);

      memset(page->vaddr, 0, PAGE_SIZE);

      Info("getblk() recycled/alloced page:%08x, paddr:%08x", (uint32_t)page, (uint32_t)page->physical_addr);
    
      return page;
    }
  }
}


/*
 * TODO: Add a timeout of 10 seconds, or maybe pass in an absolute timeout.
 */
struct Page *getblk_anon(void)
{
  struct Page *page;
    
  while(1) {
    if ((page = find_available_blk()) == NULL) {
      Info("getblk_anon() - sleeping");
      TaskSleep(&page_list_rendez);
      continue;
    }

    KASSERT((page->bflags & B_BUSY) == 0);

    page->bflags |= B_BUSY;

    remove_from_free_page_queue(page);

    // Buf is valid
    if (page->bflags & B_VALID) {
      remove_from_lookup_page_hash(page);
      remove_from_vnode_page_list(page);

      page->bflags &= ~B_VALID;
    }
    
    page->vnode = NULL;

    page->file_offset = 0;
        
    memset(page->vaddr, 0, PAGE_SIZE);

    Info("getblk_anon() page:%08x, paddr:%08x", (uint32_t)page, (uint32_t) page->physical_addr);


    return page;
  }
}


/*
 *
 * TODO: Should we have a background task clearing anon pages
 */
void putblk_anon(struct Page *page)
{
  add_to_free_page_queue_tail(page);

  page->bflags &= ~B_BUSY;
    
  TaskWakeupAll(&page_list_rendez);  
}


/* @brief   Find a specific file's block in the cache
 *
 * @param   vnode, file to find block of
 * @param   file_offset, offset within the file (aligned to page size)
 * @return  Page on success, null if not present
 */
struct Page *find_blk(struct VNode *vnode, uint64_t file_offset)
{
  struct Page *page;
  int h;
  
  h = calc_page_lookup_hash(vnode->inode_nr, file_offset);
  page = LIST_HEAD(&page_lookup_hash[h]);

  while (page != NULL) {
    if (page->vnode == vnode && page->file_offset == file_offset) {
      return page;
    }
    
    page = LIST_NEXT(page, lookup_link);
  }
  
  return NULL;
}


/* @brief   Find any block that doesn't need to be written to disk
 *
 * @return  Page on success, null if not present
 *
  // Do we need to remove pages that are busy in getblk, or do we
  // Need to skip over busy pages in the search?
 *
 *
 * Remove from appropriate LRU list,  if valid it is still on the hash lookup 
+ */
struct Page *find_available_blk(void)
{
  struct Page *page;

  if ((page = LIST_TAIL(&free_page_queue)) != NULL) {
    return page;
  }

  return NULL;
}


/* @brief   Add a page to the free page queue
 *
 */
void add_to_free_page_queue(struct Page *page)
{
  LIST_ADD_HEAD(&free_page_queue, page, free_link);
  free_page_cnt++;
}


/* @brief   Add a page to the free page queue tail
 *
 */
void add_to_free_page_queue_tail(struct Page *page)
{
  LIST_ADD_TAIL(&free_page_queue, page, free_link);
  free_page_cnt++;
}


/* @brief   Remove a page from a page queue
 *
 */
void remove_from_free_page_queue(struct Page *page)
{
  LIST_REM_ENTRY(&free_page_queue, page, free_link);
  free_page_cnt--;
}


/*
 *
 */ 
void add_to_lookup_page_hash(struct Page *page)
{
  int h = calc_page_lookup_hash(page->vnode->inode_nr, page->file_offset);
  LIST_ADD_HEAD(&page_lookup_hash[h], page, lookup_link);
}


/*
 *
 */ 
void remove_from_lookup_page_hash(struct Page *page)
{
  int h = calc_page_lookup_hash(page->vnode->inode_nr, page->file_offset);
  LIST_REM_ENTRY(&page_lookup_hash[h], page, lookup_link);
}


/*
 *
 */ 
void add_to_vnode_page_list(struct Page *page)
{
  KASSERT(page->vnode != NULL);
  
  LIST_ADD_HEAD(&page->vnode->page_list, page, vnode_link);
}


/*
 *
 */ 
void remove_from_vnode_page_list(struct Page *page)
{
  KASSERT(page->vnode != NULL);
  KASSERT(page->vnode->superblock != NULL);

  LIST_REM_ENTRY(&page->vnode->page_list, page, vnode_link);
}


/*
 * TODO: Use minix hash algorithm
 */
int calc_page_lookup_hash(ino_t inode_nr, off64_t file_offset)
{
  uint32_t a = (uint32_t)inode_nr;
  uint32_t b = (uint32_t)file_offset;
  uint32_t c = (uint32_t)(file_offset >> 32);

	hash_mix(a, b, c);
	hash_final(a, b, c);

  return c % PAGE_LOOKUP_HASH_SZ;
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
struct Page *bread(struct VNode *vnode, off64_t file_offset)
{
  struct Page *page;
  ssize_t xfered;

  Info("bread(vnode:%08x, offs:%08x", (uint32_t)vnode, (uint32_t)file_offset);

  page = getblk(vnode, file_offset);

  if (page == NULL) {
    Info("bread failed, getblk failed");
    return NULL;
  }

  if (page->bflags & B_VALID) {
    return page;
  }
    
  xfered = vfs_read(vnode, KUCOPY, page->vaddr, PAGE_SIZE, &file_offset);

  KASSERT(xfered <= PAGE_SIZE);
  
	if (xfered <= 0) {
	  Error("bread failed, xfered = %d", (int)xfered);
		page->bflags |= B_ERROR;
    brelse(page);
    return NULL;
  }

	if (xfered < PAGE_SIZE) {
		memset(page->vaddr + xfered, 0, PAGE_SIZE - xfered);
  }

  page->bflags |= B_VALID;

  return page;
}


/*
 * TODO: getblk, can it return NULL ?
 *
 * TODO: optimization, if not present, can we set a flag so it gets from clean pool ?
 */
struct Page *bread_zero(struct VNode *vnode, off64_t file_offset)
{
  struct Page *page;

  page = getblk(vnode, file_offset);

  memset(page->vaddr, 0, PAGE_SIZE);

  page->bflags |= B_VALID;
  return page;
}


/* @brief   Writes a block to disk and releases it. Waits for IO to complete.
 * 
 * @param   page, buffer to write
 * @return  0 on success, negative errno on failure
 *
 * TODO: in file.c read_from_file() can we grab a bunch of pages for larger writes from the cache?
 * Then do a single message to write all data at once?
 */
int bwrite(struct Page *page)
{
  ssize_t xfered;
  struct VNode *vnode;
  off64_t file_offset;
  off_t nbytes_to_write;
    
  vnode = page->vnode;
  file_offset = page->file_offset;

  if ((vnode->size - page->file_offset) < PAGE_SIZE) {
    nbytes_to_write = vnode->size % PAGE_SIZE;
  } else {
    nbytes_to_write = PAGE_SIZE;
  }

  xfered = vfs_write(vnode, KUCOPY, page->vaddr, nbytes_to_write, &file_offset);

  if (xfered != nbytes_to_write) {
    page->bflags |= B_ERROR;
    brelse(page);
    return -1;
  }

  brelse(page);
  return 0;
}


/* @brief   Discard a buffer in the cache, removing it from a vnode
 *
 * @param   page, buffer to discard
 * @return  0 on success, negative errno on failure
 *
 * TODO: Remove buffer from any vnode lists it is on.  (busy pages aren't on the vnode lists).
 */
int bdiscard(struct Page *page)
{
  page->bflags |= B_DISCARD;
  brelse(page);
  return 0;
}


/* @brief   Release a cache block
 *
 * @param   page, buf to be be released
 */
void brelse(struct Page *page)
{
  Info("brelse() page:%08x", (uint32_t)page);
  
  if (page->bflags & (B_ERROR | B_DISCARD)) {
    if (page->bflags & B_ERROR) {
      Error("File Block Error");
    }
    
    remove_from_lookup_page_hash(page);    
    remove_from_vnode_page_list(page);

    page->file_offset = 0;
    page->vnode = NULL;
    page->bflags = 0;
    add_to_free_page_queue_tail(page);

  } else {      
    page->bflags &= ~B_BUSY;
    add_to_free_page_queue(page);
  }

  TaskWakeupAll(&page_list_rendez);
  TaskWakeupAll(&page->rendez);
}


/* @brief   Mark all pages belonging to a file in the cache as busy
 *
 * The purpose is to ensure no pages can be removed from the vnode->page_list
 * whilst truncatev is walking the list.
 */
void mark_all_vnode_pages_as_busy(struct VNode *vnode)
{
  struct Page *page;

  page = LIST_HEAD(&vnode->page_list);
  
  while (page != NULL) {
    page->bflags |= B_BUSY;
    page = LIST_NEXT(page, vnode_link);
  }
}


/* @brief   Sync all dirty blocks of a vnode to disk
 *
 * @param   vnode, file to sync
 * @return  0 on success, negative errno on failure
 *
 * Called with vnode exclusive locked, maybe also the superblock vnode_list locked/busy or rwlock
 *
 * TODO: There should be no dirty blocks in cache (there should be no dirty bit or dirty list).
 */
int bsyncv(struct VNode *vnode)
{
  int sc = 0;
  struct Page *page;
  off64_t nbytes_to_write;
  
  Info("bsyncv()");

  KASSERT(vnode->lock.exclusive_cnt == 1 || vnode->lock.is_draining == true);

  // vfs_sync_file(vnode)   // TODO: Send message to sync file

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
 * The new size must already be set within the vnode structure and the vnode
 * should already have an exclusive lock.
 *
 * TODO: Maybe send a truncate message to the filesystem handler in here
 */
int btruncatev(struct VNode *vnode)
{
  struct Page *page;
  struct Page *next;
  off64_t cluster_offset;
  off64_t remaining;

  KASSERT(vnode->lock.exclusive_cnt == 1 || vnode->lock.is_draining == true);

  mark_all_vnode_pages_as_busy(vnode);

  page = LIST_HEAD(&vnode->page_list);
    
  while(page != NULL) {
    next = LIST_NEXT(page, vnode_link); 
  
    if (vnode->size <= page->file_offset) {
      page->bflags &= ~B_VALID;
      
      remove_from_free_page_queue(page);        
      remove_from_lookup_page_hash(page);
      remove_from_vnode_page_list(page);

      bdiscard(page);
         
    } else if ((vnode->size - page->file_offset) < PAGE_SIZE) {
      // Clear partial buf at end of file, write page immediately
      cluster_offset = vnode->size - page->file_offset;
      remaining = PAGE_SIZE - cluster_offset;
      
      memset(page->vaddr + cluster_offset, 0, remaining);

      bwrite(page);
    }
      
    page = next;
  }
  
  return 0;
}

  
/* @brief   Invalidate all blocks of a vnode, discarding them without writing to disk.
 *
 * @param   vnode, file to truncate
 * @return  0 on success, negative errno on failure
 *
 * Used when deleting a file OR repurposing a free vnode
 *
 * TODO: Note:  vnode_find/vnode_new needs to be able to clear this.
 *
 * TODO: Do we send an vfs_invalidate command here or does vfs_unlink/etc handle the FS handler flush?
 *       Invalidate could be for other reasons such as recycling a vnode.
 */
int binvalidatev(struct VNode *vnode)
{
  struct Page *page;

  // Does it really need exclusive lock?
  KASSERT(vnode->lock.exclusive_cnt == 1 || vnode->lock.is_draining == true);

  while((page = LIST_HEAD(&vnode->page_list)) != NULL) {
    page->bflags |= B_BUSY;
    page->bflags &= ~B_VALID;
    
    remove_from_free_page_queue(page);        
    remove_from_lookup_page_hash(page);
    remove_from_vnode_page_list(page);

    bdiscard(page);
  }

  return 0;
}



