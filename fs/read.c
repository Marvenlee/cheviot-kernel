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
#include <sys/mount.h>
#include <sys/syslimits.h>

/* @brief   Read the contents of a file to a buffer
 *
 * @param   fd, file descriptor of file to read from
 * @param   dst, user-mode buffer to read data from file into
 * @param   sz, size in bytes of buffer pointed to by dst
 * @return  number of bytes read or negative errno on failure
 *
 * TODO:
 * Can VNode lock fail?  Can we do multiple readers/single writer ?
 * Can we update access timestamps lazily?
 * Can is_allowed take the filp pointer, to merge open oflags test?
 */
ssize_t sys_read(int fd, void *dst, size_t sz)
{
  struct Filp *filp;
  struct VNode *vnode;
  ssize_t xfered;
  struct Process *current;
  int sc;
  
  if ((sc = bounds_check(dst, sz)) != 0) {
    return sc;
  }  
    
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EBADF;
  }

  vn_lock(vnode, VL_SHARED);
  
  if (is_allowed(vnode, R_OK) != 0) {
    vn_lock(vnode, VL_RELEASE);
    return -EACCES;
  }

#if 0
  if (filp->flags & O_READ) == 0) {
    vn_lock(vnode, VL_RELEASE);
    return -EACCES;
  } 
#endif  
  
  if (S_ISCHR(vnode->mode)) {
    xfered = read_from_char (vnode, dst, sz);
  } else if (S_ISREG(vnode->mode)) {
    xfered = read_from_cache (vnode, dst, sz, &filp->offset, false);
  } else if (S_ISFIFO(vnode->mode)) {
    xfered = read_from_pipe (vnode, dst, sz);  
  } else if (S_ISBLK(vnode->mode)) {
    xfered = read_from_block (vnode, dst, sz, &filp->offset);
  } else if (S_ISDIR(vnode->mode)) {
    xfered = -EBADF;
  } else if (S_ISSOCK(vnode->mode)) {
    xfered = -EBADF;
  } else {
    xfered = -EBADF;
  }
  
  // Update accesss timestamps
  
  vn_lock(vnode, VL_RELEASE);
  return xfered;
}


/* @brief   Read from a file to a kernel buffer
 *
 */
ssize_t kread(int fd, void *dst, size_t sz)
{
  struct Filp *filp;
  struct VNode *vnode;
  ssize_t xfered;
  struct Process *current;
  int sc;
  
  if ((sc = bounds_check_kernel(dst, sz)) != 0) {
    return sc;
  }  
    
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EBADF;
  }

  vn_lock(vnode, VL_SHARED);
  
  if (is_allowed(vnode, R_OK) != 0) {
    vn_lock(vnode, VL_RELEASE);
    return -EACCES;
  }

#if 0
  if (filp->flags & O_READ) == 0) {
    vn_lock(vnode, VL_RELEASE);
    return -EACCES;
  } 
#endif  
  
  if (S_ISREG(vnode->mode)) {
    xfered = read_from_cache (vnode, dst, sz, &filp->offset, true);
  } else {
    xfered = -EBADF;
  }

  // Update accesss timestamps
    
  vn_lock(vnode, VL_RELEASE);
  return xfered;
}


/*
 * TODO: Check bounds of each IOV
 */
ssize_t sys_preadv(int fd, msgiov_t *_iov, int iov_cnt, off64_t *_offset)
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

  vn_lock(vnode, VL_SHARED);

  if (is_allowed(vnode, R_OK) != 0) {
    vn_lock(vnode, VL_RELEASE);
    return -EACCES;
  }

#if 0 
  if (filp->flags & O_READ) == 0) {
    return -EACCES;
  } 
#endif  

  if (S_ISBLK(vnode->mode)) {
    if (_offset == NULL) {
      xfered = read_from_blockv (vnode, iov, iov_cnt, &filp->offset);
    } else {
      xfered = read_from_blockv (vnode, iov, iov_cnt, &offset);
    }   
  } else {
    xfered = -EBADF;
  }
  
  // Update accesss timestamps
  
  vn_lock(vnode, VL_RELEASE);
  return xfered;
}



