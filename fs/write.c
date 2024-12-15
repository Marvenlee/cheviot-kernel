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
 * Write to a file
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <sys/mount.h>
#include <sys/syslimits.h>


/* @brief   Write the contents of a buffer to a file
 *
 * @param   fd, file descriptor of file to write to
 * @param   src, user-mode buffer containing data to write to file
 * @param   sz, size in bytes of buffer pointed to by src
 * @return  number of bytes written or negative errno on failure
 *
 * TODO: Update accesss timestamps
 */
ssize_t sys_write(int fd, void *src, size_t sz)
{
  struct Filp *filp;
  struct VNode *vnode;
  ssize_t xfered;
  struct Process *current;
  int sc;
  
  if ((sc = bounds_check(src, sz)) != 0) {
    return sc;
  }  
  
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  rwlock(&vnode->lock, LK_EXCLUSIVE);
  
  if (check_access(vnode, filp, W_OK) != 0) {
    rwlock(&vnode->lock, LK_RELEASE);
    return -EACCES;
  }
    
  if (S_ISCHR(vnode->mode)) {
    xfered = write_to_char(vnode, src, sz);  
  } else if (S_ISREG(vnode->mode)) {
    xfered = write_to_cache(vnode, src, sz, &filp->offset);
  } else if (S_ISBLK(vnode->mode)) {
    xfered = write_to_block(vnode, src, sz, &filp->offset);
  } else if (S_ISFIFO(vnode->mode)) {
    xfered = write_to_pipe(vnode, src, sz);
  } else {
    Error("sys_write fd:%d unknown type -EINVAL", fd);
    xfered = -EINVAL;
  }  
  
  rwlock(&vnode->lock, LK_RELEASE);
  return xfered;
}


/*
 * TODO: Check bounds of each IOV
 * TODO: Update accesss timestamps
 */
ssize_t sys_pwritev(int fd, msgiov_t *_iov, int iov_cnt, off64_t *_offset)
{
  off64_t offset;
  struct Filp *filp;
  struct VNode *vnode;
  ssize_t xfered;
  struct Process *current;
  msgiov_t iov[IOV_MAX];
    
  if (iov_cnt < 1 || iov_cnt > IOV_MAX) {
    return -EINVAL;
  }
  
  if (CopyIn(iov, _iov, sizeof(msgiov_t) * iov_cnt) != 0) {
    return -EFAULT;
  } 

  if (_offset != NULL) {
    if (CopyIn(&offset, _offset, sizeof(off64_t)) != 0) {
      return -EFAULT;
    } 
  }
      
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EBADF;
  }

  rwlock(&vnode->lock, LK_EXCLUSIVE);
  
  if (check_access(vnode, filp, W_OK) != 0) {
    rwlock(&vnode->lock, LK_RELEASE);
    return -EACCES;
  }

  if (S_ISBLK(vnode->mode)) {
    if (_offset == NULL) {
      xfered = write_to_blockv (vnode, iov, iov_cnt, &filp->offset);
    } else {
      xfered = write_to_blockv (vnode, iov, iov_cnt, &offset);
    }   
  } else {
    xfered = -EBADF;
  }

  rwlock(&vnode->lock, LK_RELEASE);
  
  return xfered;
}

