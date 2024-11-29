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
#include <kernel/utility.h>
#include <poll.h>
#include <string.h>
#include <kernel/kqueue.h>


/* @brief   Change the current directory
 *
 * @param   _path, directory pathname to change to
 * @return  0 on success, negative errno on failure
 */
int sys_chdir(char *_path)
{
  struct Process *current;
  struct lookupdata ld;
  int err;
  
  Info("sys_chdir()");

  current = get_current_process();

  if ((err = lookup(_path, 0, &ld)) != 0) {
    return err;
  }

  if (!S_ISDIR(ld.vnode->mode)) {
    lookup_cleanup(&ld);
    return -ENOTDIR;
  }

  if (check_access(ld.vnode, NULL, R_OK) != 0) {
    lookup_cleanup(&ld);
    return -EPERM;
  }

  if (current->fproc->current_dir != NULL) {
    vnode_put(current->fproc->current_dir);
  }

  vnode_add_reference(ld.vnode);
  current->fproc->current_dir = ld.vnode;

  vn_lock(current->fproc->current_dir, VL_RELEASE);
  lookup_cleanup(&ld);
  return 0;
}


/* @brief   Change the current directory to that referenced by a file handle
 *
 * @param   fd, file handle to an existing open directory
 * @return  0 on success, negative errno on failure
 */
int sys_fchdir(int fd)
{
  struct Process *current;
  struct VNode *vnode;

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (!S_ISDIR(vnode->mode)) {
    return -ENOTDIR;
  }

  if (check_access(vnode, NULL, R_OK) != 0) {
    return -EPERM;
  }

  vnode_put(current->fproc->current_dir);
  current->fproc->current_dir = vnode;
  vnode_add_reference(vnode);
  return 0;
}


/* @brief   Change the root directory of the current process
 */ 
int sys_chroot(char *_new_root)
{
  return -ENOSYS;
}


/* @brief   Open a directory for reading
 * 
 * @param   _path, the pathname of a directory to open
 * @return  0 on success, negative errno on failure
 */
int sys_opendir(char *_path)
{
  struct Process *current;
  struct lookupdata ld;
  struct Filp *filp = NULL;
  int fd;
  int sc;

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) != 0) {
    return sc;
  }

  if (!S_ISDIR(ld.vnode->mode)) {
    lookup_cleanup(&ld);
    return -EINVAL;
  }

  if (check_access(ld.vnode, NULL, R_OK) != 0) {
    lookup_cleanup(&ld);
    return -EPERM;
  }

  fd = alloc_fd_filp(current);

  if (fd < 0) {
    free_fd_filp(current, fd);
    lookup_cleanup(&ld);
    return -ENOMEM;
  }

  filp = get_filp(current,fd);
  filp->type = FILP_TYPE_VNODE;
  filp->u.vnode = ld.vnode;
  filp->offset = 0;

  vnode_add_reference(ld.vnode);  
  lookup_cleanup(&ld);
  return fd;
}


/* @brief   Read the contents of a directory into a buffer
 * 
 * @param   fd, file handle to a directory opened by sys_opendir()
 * @param   dst, pointer to user-mode buffer to read directory entries into
 * @param   sz, size of user-mode buffer
 * @return  number of bytes read, 0 indicates end of directory, negative errno values on failure
 */
ssize_t sys_readdir(int fd, void *dst, size_t sz)
{
  struct Filp *filp = NULL;
  struct VNode *vnode = NULL;
  ssize_t dirents_sz;
  off64_t cookie;
  struct Process *current;

  if (sz < MIN_READDIR_BUF_SZ) {     // TODO: Ensure size is big enough for name_max + 1 + sizeof struct dirent.
    return -EINVAL;
  }

  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (!S_ISDIR(vnode->mode)) {
    return -ENOTDIR;
  }
  
  cookie = filp->offset;

  vn_lock(vnode, VL_SHARED);
  
  dirents_sz = vfs_readdir(vnode, dst, sz, &cookie);
  filp->offset = cookie;
  
  vn_lock(vnode, VL_RELEASE);
  return dirents_sz;
}


/* @brief   Seek to the beginning of a directory
 *
 * @param   fd, file handle to directory opened with opendir()
 * @return  0 on success, negative errno on failure
 */
int sys_rewinddir(int fd)
{
  struct Filp *filp = NULL;
  struct VNode *vnode = NULL;
  struct Process *current;
  
  current = get_current_process();
  filp = get_filp(current, fd);
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (!S_ISDIR(vnode->mode)) {
    return -EINVAL;
  }

  filp->offset = 0;
  return 0;
}


/* @brief   Create a new directory
 *
 * @param   _path, pathname of new directory to create
 * @param   mode, ownership permissions of the new directory
 * @return  0 on success, negative errno on failure
 *
 * TODO: Check if dvnode is covered_vnode or covering vnode at mount points
 */
int sys_createdir(char *_path, mode_t mode)
{
  struct lookupdata ld;
  struct stat stat;
  struct VNode *dvnode;
  int sc;

  if ((sc = lookup(_path, LOOKUP_PARENT, &ld)) != 0) {
    return sc;
  }

  if (check_access(ld.parent, NULL, R_OK) != 0) {
    lookup_cleanup(&ld);
    return -EPERM;
  }
  
  if (ld.vnode != NULL) {
    // already exists, check if it is a directory
    if (S_ISDIR(ld.vnode->mode)) {
      lookup_cleanup(&ld);
      return 0;
    } else {
      lookup_cleanup(&ld);
      return -ENOTDIR;
    }  
  }
  
  dvnode = ld.parent;
  
  vn_lock(dvnode, VL_EXCLUSIVE);

  sc = vfs_mkdir(dvnode, ld.last_component, &stat);

  if (sc != 0) {
    vn_lock(dvnode, VL_RELEASE); 
    lookup_cleanup(&ld);
    return sc;
  }    

  knote(&dvnode->knote_list, NOTE_WRITE | NOTE_ATTRIB);

  vn_lock(dvnode, VL_RELEASE); 
  lookup_cleanup(&ld);
  return 0;
}


/* @brief   Remove an existing directory
 * 
 * @param   _path, pathname of the directory to delete
 * @return  0 on success, negative errno on failure
 */
int sys_rmdir(char *_path)
{
  struct lookupdata ld;
  struct VNode *vnode = NULL;
  struct VNode *dvnode = NULL;
  int sc = 0;

  if ((sc = lookup(_path, LOOKUP_REMOVE, &ld)) != 0) {
    return sc;
  }
  
  vnode = ld.vnode;
  dvnode = ld.parent;

  if (!S_ISDIR(vnode->mode)) {
    lookup_cleanup(&ld);
    return -ENOTDIR;
  }

  vn_lock(dvnode, VL_EXCLUSIVE);    // Exclusive lock to remove entries from directory
  vn_lock(vnode, VL_DRAIN);       // Drain lock to remove vnode from directory

  sc = vfs_rmdir(dvnode, vnode, ld.last_component);   
  
  if (sc != 0) {
    ld.vnode = NULL;
  }

  knote(&dvnode->knote_list, NOTE_WRITE | NOTE_ATTRIB);  

  vn_lock(dvnode, VL_RELEASE);
  lookup_cleanup(&ld);
  return 0;
}


