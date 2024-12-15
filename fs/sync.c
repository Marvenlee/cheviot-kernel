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


/* @brief   Write all mounted filesystems to disk
 *
 * Send sync message to all mounted filesystems
 *
 * TODO: Do we increment superblock reference count, then decrement once finished ?
 */
int sys_sync(void)
{
  struct SuperBlock *sb;
  int saved_sc = 0;
  
  Info("sys_sync()");
  
  rwlock(&mounted_sb_list_lock, LK_SHARED);
  
  sb = LIST_HEAD(&mounted_superblock_list);
  
  while (sb != NULL) {
    if (sb->root != NULL && S_ISDIR(sb->root->mode) && (sb->flags & SF_READONLY) == 0) {
      int sc = bsyncfs(sb, BSYNC_ALL_NOW);
      if (sc != 0 && saved_sc == 0) {
        saved_sc = sc;
      }
    }
        
    sb = LIST_NEXT(sb, link);
  }

  rwlock(&mounted_sb_list_lock, LK_RELEASE);

  Info("sys_sync() DONE, sc:%d", saved_sc);

  return saved_sc;
}


/* @brief   Write all unwritten blocks of a file to disk
 *
 */
int sys_fsync(int fd)
{ 
  struct Process *current;
  struct VNode *vnode;
  int sc;
  
  Info("sys_fsync(%d)", fd);
  
  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (S_ISREG(vnode->mode) == 0) {
    return -EINVAL;
  }

  if (check_access(vnode, NULL, W_OK) != 0) {
    return -EACCES;
  }
  
  rwlock(&vnode->lock, LK_EXCLUSIVE);

  sc = bsync(vnode, BSYNC_ALL_NOW);
  
  rwlock(&vnode->lock, LK_RELEASE);
  
  return sc;  
}


/* @brief   Force a filesystem sync from the server side
 *
 * @param   fd, file descriptor of server message port
 * @param   shutdown, if true then prevent further access to filesystem
 * @return  0 on success, negative errno on failure
 * 
 * This is a non-blocking function.
 */
int sys_sync2(int fd, bool shutdown)
{
  // Notify bdflush task to sync

  return -ENOSYS;
}



