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
int sys_truncate(int fd, size_t sz) {
  struct Process *current;
  struct Filp *filp = NULL;
  struct VNode *vnode = NULL;
  int sc = 0;

  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  vn_lock(vnode, VL_EXCLUSIVE);
  
  if (!S_ISREG(vnode->mode)) {
    Error("truncate: vnode is not reg file!");
    vn_lock(vnode, VL_RELEASE);
    return -EINVAL;
  }

  if ((sc = vfs_truncate(vnode, 0)) != 0) {
    vn_lock(vnode, VL_RELEASE);
    return sc;
  }

  // TODO: Check if size has gone up or down.
  knote(&vnode->knote_list, NOTE_EXTEND | NOTE_ATTRIB);

  vn_lock(vnode, VL_RELEASE);

  return 0;
}




