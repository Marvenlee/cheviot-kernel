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
#include <limits.h>


/* @brief   Read from a block device
 * 
 * @param   vnode, vnode of block device
 * @param   dst, buffer to read to
 * @param   sz, buffer size
 * @param   offset, pointer to the filp's offset, this is updated.
 */
ssize_t read_from_block(struct VNode *vnode, void *dst, size_t sz, off64_t *offset)
{
  size_t total_xfered = 0;

  while (total_xfered < sz) {
    size_t remaining = sz - total_xfered;
    ssize_t xfered = vfs_read(vnode, IPCOPY, dst, remaining, offset);
    
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
	size_t total_xfered = 0;

  while (total_xfered < sz) {
    size_t remaining = sz - total_xfered;
    ssize_t xfered = vfs_write(vnode, IPCOPY, src, remaining, offset);      
  
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


/*
 *
 */
ssize_t read_from_blockv(struct VNode *vnode, msgiov_t *iov, int iov_cnt, off64_t *offset)
{
  ssize_t xfer = 0;
  ssize_t xfered;
  
  for (int t=0; t<iov_cnt; t++) {
    xfer += iov->size;
  }

  xfered = vfs_readv(vnode, IPCOPY, iov, iov_cnt, xfer, offset);
    
  return xfered;
}


/*
 *
 */
ssize_t write_to_blockv(struct VNode *vnode, msgiov_t *iov, int iov_cnt, off64_t *offset)
{
  ssize_t xfer = 0;
  ssize_t xfered;
  
  for (int t=0; t<iov_cnt; t++) {
    xfer += iov->size;
  }

  xfered = vfs_writev(vnode, IPCOPY, iov, iov_cnt, xfer, offset);
    
  return xfered;
}

