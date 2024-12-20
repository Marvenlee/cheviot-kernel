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

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>


/* @brief   Write all mounted filesystems to disk
 *
 * Send sync message to all mounted filesystems
 */
int sys_sync(void)
{
/*
  struct SuperBlock *sb;
  
  sb = LIST_HEAD(&mounted_superblock_list);
  
  while (sb != NULL) {
    //    sc = vfs_syncfs(sb);
  
    sb = LIST_NEXT(sb, mounted_superblock_link);
  }
*/    
  return -ENOSYS;
}


/* @brief   Write all unwritten blocks of a file to disk
 *
 */
int sys_fsync(int fd)
{ 
  struct Process *current;
  struct VNode *vnode;
  int sc;
  
  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (check_access(vnode, NULL, W_OK) != 0) {
    return -EACCES;
  }
  
  vn_lock(vnode, VL_SHARED);

  // TODO:  Sync all blocks of file
  
  vn_lock(vnode, VL_RELEASE);
  
  return sc;  
}


/* @brief   Force a filesystem sync from the server side
 *
 * @param   fd, file descriptor of server message port
 * @param   unmount, force an unmount from the filesystem to prevent further changes
 *
 * This is a non-blocking function.
 */
int sys_sync2(int fd, bool unmount)
{
  return -ENOSYS;
}



