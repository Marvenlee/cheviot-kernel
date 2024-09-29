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
#include <kernel/types.h>
#include <kernel/vm.h>
#include <sys/fsreq.h>
#include <sys/mount.h>


// FIXME: Unsure why but transfers of 1024 (2 blocks) is dramatically slower
// A larger transfer size should be faster with less context switches and system calls.
#define MAX_BLOCK_TRANSFER 512


/* @brief   Read from a block device
 * 
 * @param   vnode, vnode of block device
 * @param   dst, buffer to read to
 * @param   sz, buffer size
 * @param   offset, pointer to the filp's offset, this is updated.
 */
ssize_t read_from_block(struct VNode *vnode, void *dst, size_t sz, off64_t *offset)
{
  ssize_t xfered = 0;
  size_t xfer = 0;
  size_t total_xfered = 0;

  while (total_xfered < sz) {
    xfer = ((sz - total_xfered) < MAX_BLOCK_TRANSFER) ? (sz - total_xfered) : MAX_BLOCK_TRANSFER;
    xfered = vfs_read(vnode, IPCOPY, dst, xfer, offset);
    
    if (xfered == 0) {
      return total_xfered;
    }
    
    if (xfered < 0) {
      if (total_xfered > 0) {
        return total_xfered;
      } else {
        return xfered;
      }
    }

    dst += xfered;  
    total_xfered += xfered;
  }

  return total_xfered;
}


/* @brief   Write to a block device
 */
ssize_t write_to_block(struct VNode *vnode, void *src, size_t sz, off64_t *offset)
{
  ssize_t xfered = 0;
  size_t xfer = 0;
	size_t total_xfered = 0;

  while (total_xfered < sz) {
    xfer = ((sz - total_xfered) < MAX_BLOCK_TRANSFER) ? (sz - total_xfered) : MAX_BLOCK_TRANSFER;
    xfered = vfs_write(vnode, IPCOPY, src, xfer, offset);      
  
    if (xfered == 0) {
      return total_xfered;
    }
        
    if (xfered < 0) {
      if (total_xfered > 0) {
        return total_xfered;
      } else {
        return xfered;
      }
    }

    src += xfered;
    total_xfered += xfered;
  }

  return total_xfered;
}

