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
ssize_t read_from_file(struct VNode *vnode, void *dst, size_t sz, off64_t *offset, bool inkernel)
{
  struct Page *page;
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

  remaining_in_file = vnode->size - *offset;

  nbytes_total = 0;
  nbytes_to_read = (remaining_in_file < sz) ? remaining_in_file : sz;

  while (nbytes_total < nbytes_to_read) {  
    cluster_base = ALIGN_DOWN(*offset, PAGE_SIZE);
    cluster_offset = *offset % PAGE_SIZE;

    remaining_to_xfer = nbytes_to_read - nbytes_total;
    remaining_in_cluster = PAGE_SIZE - cluster_offset;
    nbytes_xfer = (remaining_to_xfer < remaining_in_cluster) ? remaining_to_xfer : remaining_in_cluster;
		
    page = bread(vnode, cluster_base);

    if (page == NULL) {
      Warn("bread page is NULL");
      
      if (nbytes_total > 0) {
        return nbytes_total;
      } else {
        return -EIO;
      }
    }

    if (inkernel == true) {
      memcpy(dst, page->vaddr + cluster_offset, nbytes_xfer);
    } else {    
      if (CopyOut(dst, page->vaddr + cluster_offset, nbytes_xfer) != 0) {
      	return -EFAULT;
      }
    }
    
    // FIXME: Could have been a dirty page, if so need to return it to the dirty queue
    brelse(page);
        
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
ssize_t write_to_file(struct VNode *vnode, void *src, size_t sz, off64_t *offset)
{
  struct Page *page;
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
      page = bread(vnode, cluster_base);
    } else {
      page = bread_zero(vnode, cluster_base);
    }
    
    if (page == NULL) {
      if (nbytes_total > 0) {
        return nbytes_total;
      } else {
        return -EIO;
      }
    }

    if (CopyIn(page->vaddr + cluster_offset, src, nbytes_xfer) != 0) {
      return -EFAULT;  
    }
		 
    src += nbytes_xfer;
    *offset += nbytes_xfer;
    nbytes_total += nbytes_xfer;

    // Update file size if we have written past the end of file    
    if (*offset > vnode->size) {
      vnode->size = *offset;
    }

    // TODO: Allow option to send vfs messages with less than a page or block of data
    // TODO: Also allow preallocation of cache buffers so that multiple pages can be
    // written.
    // If filesystem handler doesn't have the page we are partially writing to in it's
    // cache, allow it to return an -EAGAIN error, to indicate the cache should resend
    // the whole page, to avoid reading from disk.
    
    // We may want to set a flag B_SYNC to indicate it won't be used again soon, so that
    // it can be removed from the FS handler's cache.  (after writing to end of block
    // for example).  Similarly mark as B_DELWRI to indicate to FS handler to keep
    // block around as more data will be written to it.
    // Or FS handler could determine it if the size of a write hits the end of a full
    // page.
    
    bwrite(page);
  }

  return nbytes_total;
}


