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
#include <poll.h>
#include <string.h>

/* @brief   Resize an open file
 *
 */
int sys_truncate(int fd, size_t sz)
{
  struct Process *current;
  struct VNode *vnode;
  struct Filp *filp;
  int sc = 0;

  current = get_current_process();

  filp = filp_get(current, fd);
  
  if (filp) {  
    vnode = vnode_get_from_filp(filp);

    if (vnode) {
      rwlock(&vnode->lock, LK_EXCLUSIVE);
      
      if (!S_ISREG(vnode->mode)) {
        Error("truncate: vnode is not reg file!");
        rwlock(&vnode->lock, LK_RELEASE);
        return -EINVAL;
      }

      if ((sc = vfs_truncate(vnode, 0)) != 0) {
        rwlock(&vnode->lock, LK_RELEASE);
        return sc;
      }

      // TODO: Check if size has gone up or down.
      knote(&vnode->knote_list, NOTE_EXTEND | NOTE_ATTRIB);
      rwlock(&vnode->lock, LK_RELEASE);
      return 0;
      
    } else {
      return -EFAULT;
    }    
  } else {
    return -EBADF;
  }
}


