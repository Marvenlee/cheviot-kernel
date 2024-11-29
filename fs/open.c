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
 * Open a file
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
#include <sys/privileges.h>


/* @brief   Open a file
 *
 * @param   _path, pathname of file to open
 * @param   flags, options to open the file:
 *          O_RDONLY, O_WRONLY, O_RDWR, access type, one of these must be set
 *          O_CREAT, create a file if it does not exist
 *          O_APPEND, seek to the end of the file on open
 *          O_TRUNC, truncate a file to 0 bytes
 * @param   mode, optional mode access bits to apply when creating a file
 * @return  file descriptor number of success, negative errno on failure.
 */
int sys_open(char *_path, int oflags, mode_t mode)
{
  struct lookupdata ld;
  int sc;

  Info("sys_open()");

  if ((sc = lookup(_path, LOOKUP_PARENT, &ld)) != 0) {
    Error("Open - lookup failed, sc = %d", sc);
    return sc;
  }

  sc = do_open(&ld, oflags, mode);
  lookup_cleanup(&ld);
  return sc;
}


/*
 *
 */
int kopen(char *_path, int oflags, mode_t mode)
{
  struct lookupdata ld;
  int sc;

  Info("kopen()");

  if ((sc = lookup(_path, LOOKUP_PARENT | LOOKUP_KERNEL, &ld)) != 0) {
    Error("Kopen - lookup failed, sc = %d", sc);
    return sc;
  }

  sc = do_open(&ld, oflags, mode);  
  lookup_cleanup(&ld);
  return sc;  
}


/*
 * TODO: Set timestamps.
 */
int do_open(struct lookupdata *ld, int oflags, mode_t mode)
{
  struct Process *current;
  struct VNode *dvnode = NULL;
  struct VNode *vnode = NULL;
  int fd = -1;
  struct Filp *filp = NULL;
  int sc = 0;
  struct stat stat;
  
  current = get_current_process();
  vnode = ld->vnode;
  dvnode = ld->parent;
      
  if (vnode == NULL) {
    if ((oflags & O_CREAT) && check_access(dvnode, NULL, W_OK) != 0) {
      return -ENOENT;
    }

    stat.st_mode = mode;
    stat.st_uid = current->uid;
    stat.st_gid = current->gid;
		
		if (strcmp(".", ld->last_component) == 0 || strcmp("..", ld->last_component) == 0) {
      Error("Cannot create . or .. named files");
      return -ENOMEM;
    }
    
    vn_lock(dvnode, VL_EXCLUSIVE);
    
    if ((sc = vfs_create(dvnode, ld->last_component, oflags, &stat, &vnode)) != 0) {
      Error("SysOpen vnode_put vfs_create");
      vnode_put(dvnode);      
      return sc;
    }
    
    vn_lock(dvnode, VL_RELEASE);
  }
  
  vn_lock(vnode, VL_EXCLUSIVE);
   
  fd = alloc_fd_filp(current);
  
  if (fd < 0) {
    free_fd_filp(current, fd);
    vn_lock(vnode, VL_RELEASE);
    return -ENOMEM;
  }

  filp = get_filp(current, fd);
  filp->type = FILP_TYPE_VNODE;
  filp->u.vnode = vnode;
  filp->flags = oflags;

  if (oflags & O_TRUNC) {
    if (S_ISREG(vnode->mode)) {
#if 0
      if (check_access(vnode, filp, W_OK) != 0) {
        free_fd_filp(current, fd);
        vn_lock(vnode, VL_RELEASE);
        return -EACCES;
      }
#endif
      if ((sc = vfs_truncate(vnode, 0)) != 0) {
        Error("SysOpen O_TRUNC failed, sc=%d", sc);
        free_fd_filp(current, fd);
        vn_lock(vnode, VL_RELEASE);
        return sc;
      }
    }
  }
  
  if (oflags & O_APPEND) {
    filp->offset = vnode->size;
  } else {
    filp->offset = 0;
  }

  vn_lock(vnode, VL_RELEASE);
  vnode_add_reference(vnode);
  return fd;  
}


