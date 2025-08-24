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
  struct VNode *vnode;
  int saved_sc = 0;
  int sc;

  Info("sys_sync(");
  
  while (sync_in_progress == true) {
    TaskSleep(&sync_rendez);
  }
  
  sync_in_progress = true;
  
  sb = LIST_HEAD(&mounted_superblock_list);
  
  while (sb != NULL) {
    if (sb->root != NULL && S_ISDIR(sb->root->mode) && (sb->flags & SBF_READONLY) == 0) {

      sb->reference_cnt ++;
      LIST_ADD_TAIL(&sync_superblock_list, sb, sync_link);
    }      
        
    sb = LIST_NEXT(sb, link);
  }

  while((sb = LIST_HEAD(&sync_superblock_list)) != NULL) {
    LIST_REM_HEAD(&sync_superblock_list, sync_link);
    
    sc = vfs_sync(sb);
    
    if (saved_sc == 0 && sc != 0) {
      saved_sc = sc;
    }
    
    // TODO: Add a superblock_deref() function, if 0 it frees superblock?
    sb->reference_cnt--;
    TaskWakeup(&sb->rendez);
  }

  sync_in_progress = false;
  TaskWakeup(&sync_rendez);  

  return saved_sc;
}


/* @brief   Write all unwritten blocks of a file to disk
 *
 */
int sys_fsync(int fd)
{ 
  struct Filp *filp;
  struct Process *current;
  struct VNode *vnode;
  int sc;
  
  Info("sys_fsync(%d)", fd);
  
  current = get_current_process();
  
  filp = filp_get(current, fd);
  
  if (filp == NULL) {
    return -EBADF;
  }
  
  vnode = vnode_get_from_filp(filp);

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

  sc = vfs_syncfile(vnode);  // TODO: vfs message to a filesystem handler to sync a specific file
  
  rwlock(&vnode->lock, LK_RELEASE);
  
  return sc;  
}



