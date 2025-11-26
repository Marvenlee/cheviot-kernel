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

#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <sys/mount.h>


/* @brief   Get the file statistics of a named file
 *
 * @param   _path, pathname to file to gather statistics of
 * @param   _stat, pointer to stat structure to return statistics
 * @return  0 on success, negative errno on error
 */
int sys_stat(char *_path, struct stat *_stat) {
  struct stat stat;
  struct lookupdata ld;
  int sc;

  Info("sys_stat()");

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    rwlock_shared(&ld.vnode->lock);
    
    vfs_stat(ld.vnode, &stat);
    
    rwlock_release(&ld.vnode->lock);

    lookup_cleanup(&ld);

    sc = CopyOut(_stat, &stat, sizeof stat);    
  }

  Info("sys_stat sc=%d", sc);      
  return sc;
}


/* @brief   Get the file statistics of an open file
 *
 * @param   fd, file handle of file to gather statistics of
 * @param   _stat, pointer to stat structure to return statistics
 * @return  0 on success, negative errno on error 
 */
int sys_fstat(int fd, struct stat *_stat)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct stat stat;
  struct Process *current;
  int sc;
  
  Info("sys_fstat(fd:%d)", fd);

  current = get_current_process();

  // FIXME: FS what is to stop another thread from releasing a vnode whilst it is blocked waiting
  // for the rwlock? Should filp_get increment filp ref count?  Thereby preventing release of filp and vnode?

  filp = filp_get(current, fd);
  
  if (filp) {
    vnode = vnode_get_from_filp(filp);
    
    if (vnode) {
      rwlock_shared(&vnode->lock);

      vfs_stat(vnode, &stat);
    
      rwlock_release(&vnode->lock);
	    
      sc = CopyOut(_stat, &stat, sizeof stat);
    } else {
      sc = -EINVAL;
    }    

  } else {
    sc = -EBADF;
  }

  Info("sys_fstat sc=%d", sc);    
  return sc;
}





int sys_statvfs(char *_path, struct statvfs *_stat)
{
  return -ENOSYS;
}


/* @brief   Get the file statistics of an open file
 *
 * @param   fd, file handle of file to gather statistics of
 * @param   _stat, pointer to stat structure to return statistics
 * @return  0 on success, negative errno on error 
 */
int sys_fstatvfs(int fd, struct statvfs *_stat)
{
  return -ENOSYS;
}

