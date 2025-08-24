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


/* @brief   Seek to a new file position
 * @param   fd,
 * @param   pos,
 * @param   whence,
 * @return  new seek position or negative errno on error
 *
 * FIXME: Limit block seeks to stat.block size multiples?
 * filp will need to either atomically set seek position or need locking if we remove BKL
 */
off_t sys_lseek(int fd, off_t pos, int whence)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct Process *current;
  int sc;
  
  current = get_current_process();

  filp = filp_get(current, fd);
  
  if (filp) {  
    vnode = vnode_get_from_filp(filp);
    
    if (vnode) {      
      if (!S_ISREG(vnode->mode) && !S_ISBLK(vnode->mode)) {
        sc = -EINVAL;
      } else if (whence == SEEK_SET) {
        filp->offset = pos;
        sc = 0;
      } else if (whence == SEEK_CUR) {
        filp->offset += pos;
        sc = 0;
      } else if (whence == SEEK_END) {
        filp->offset = vnode->size + pos;
        sc = 0;
      } else {
        sc = -EINVAL;
      }
            
      if (sc == 0) {
        vnode_put(vnode);
        filp_put(filp);
        return (off_t)filp->offset;    
      }
      
      vnode_put(vnode);      
    } else {
      sc = -EINVAL;
    }

    filp_put(filp);
  } else {
    sc = -EBADF;
  }

  return sc;
}


/* @brief   Seek to a new file position
 * @param   fd,
 * @param   pos,
 * @param   whence,
 * @return  0 on success or negative errno on error
 */
int sys_lseek64(int fd, off64_t *_pos, int whence)
{
  struct Process *current;
  struct Filp *filp;
  struct VNode *vnode;
  off64_t pos;
  int sc;

  sc = CopyIn(&pos, _pos, sizeof pos);

  if(sc != 0) {
    return -EFAULT;
  }
  
  current = get_current_process();

  filp = filp_get(current, fd);
  
  if (filp) {
    vnode = vnode_get_from_filp(filp);

    if (vnode) {
      if (!S_ISREG(vnode->mode) && !S_ISBLK(vnode->mode)) {
        sc = -EINVAL;
      } else if (whence == SEEK_SET) {
        filp->offset = pos;
      } else if (whence == SEEK_CUR) {
        filp->offset += pos;
      } else if (whence == SEEK_END) {
        filp->offset = vnode->size + pos;
      } else {
        sc = -EINVAL;
      }

      if (sc == 0) {
        pos = filp->offset;  
        CopyOut(_pos, &pos, sizeof pos);
        vnode_put(vnode);
        filp_put(filp);
        return (off_t)filp->offset;    
      }
      
      vnode_put(vnode);      
    } else {
      sc = -EINVAL;
    }

    filp_put(filp);
  } else {
    sc = -EBADF;
  }

  return sc;


}


