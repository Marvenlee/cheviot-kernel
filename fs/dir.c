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


/* @brief   Change the current directory
 *
 * @param   _path, directory pathname to change to
 * @return  0 on success, negative errno on failure
 */
int sys_chdir(char *_path)
{
  struct Process *current;
  struct lookupdata ld;
  int sc;

  Info("sys_chdir()");
  
  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    if (S_ISDIR(ld.vnode->mode)) {
      if (check_access(ld.vnode, NULL, R_OK) == 0) {
        if (current->fproc.current_dir != NULL) {
          vnode_put(current->fproc.current_dir);
        }

        vnode_ref(ld.vnode);
        current->fproc.current_dir = ld.vnode;

        lookup_cleanup(&ld);
        return 0;        
      } else {
        sc = -EPERM;
      }
    } else {
      sc = -ENOTDIR;
    }

    lookup_cleanup(&ld);
  }

  return sc;
}


/* @brief   Change the current directory to that referenced by a file handle
 *
 * @param   fd, file handle to an existing open directory
 * @return  0 on success, negative errno on failure
 */
int sys_fchdir(int fd)
{
  struct Filp *filp;
  struct Process *current;
  struct VNode *vnode;
  int sc;

  Info("sys_fchdir()");
  
  current = get_current_process();

  filp = filp_get(current, fd);
  
  if (filp == NULL) {
    vnode = vnode_get_from_filp(filp);

    if (vnode) {
      if (!S_ISDIR(vnode->mode)) {
        if (check_access(vnode, NULL, R_OK) == 0) {
          
          if (current->fproc.current_dir != NULL) {
            vnode_put(current->fproc.current_dir);
          }
          
          current->fproc.current_dir = vnode;
          vnode_ref(vnode);
          return 0;
        } else {
          sc = -EPERM;
        }
      } else {      
        sc = -ENOTDIR;
      }
    } else {
      sc = -EINVAL;
    }
  } else {
    sc = -EBADF;
  }

  return sc;
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
  struct FileDesc *filedesc;
  int fd;
  int sc;

  Info("sys_opendir()");

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    if (S_ISDIR(ld.vnode->mode)) {
      if (check_access(ld.vnode, NULL, R_OK) == 0) {
        Info("sys_opendir calling fd_alloc()");
        fd = fd_alloc(current, 0, FILEDESC_MAX, &filedesc);

        if (fd >= 0) {
          filp = filp_get_new();

          if (filp) {
            filp->type = FILP_TYPE_VNODE;
            filp->u.vnode = ld.vnode;
            filp->offset = 0;

            vnode_ref(ld.vnode);
            lookup_cleanup(&ld);

            filedesc->filp = filp;
            filedesc->flags |= FDF_VALID;
            return fd;
          } else {
            fd_free(current, fd);
            sc = -ENOMEM;
          }
        } else {
         sc = -ENOMEM;
        }
      } else {
        sc = -EPERM;
      }
    } else {
      sc = -ENOTDIR;
    }

    lookup_cleanup(&ld);
  }

  return sc;
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
  struct Filp *filp;
  struct VNode *vnode;
  ssize_t dirents_sz;
  off64_t cookie;
  struct Process *current;

  Info("sys_readdir()");


  if (sz < MIN_READDIR_BUF_SZ) {     // TODO: Ensure size is big enough for name_max + 1 + sizeof struct dirent.
    return -EINVAL;
  }

  current = get_current_process();
  filp = filp_get(current, fd);

  if (filp == NULL) {
    return -EBADF;
  }

  vnode = vnode_get_from_filp(filp);

  if (vnode == NULL) {
    return -EINVAL;
  }

  if (!S_ISDIR(vnode->mode)) {
    return -ENOTDIR;
  }
  
  cookie = filp->offset;

  rwlock_shared(&vnode->lock);
  dirents_sz = vfs_readdir(vnode, dst, sz, &cookie);
  rwlock_release(&vnode->lock);

  filp->offset = cookie;
  
  return dirents_sz;
}


/* @brief   Seek to the beginning of a directory
 *
 * @param   fd, file handle to directory opened with opendir()
 * @return  0 on success, negative errno on failure
 */
int sys_rewinddir(int fd)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct Process *current;
  
  current = get_current_process();

  filp = filp_get(current, fd);

  if (filp == NULL) {
    return -EBADF;
  }

  vnode = vnode_get_from_filp(filp);

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
  struct VNode *dvnode;
  struct Process *current;
  int sc;

  Info("sys_createdir()");

  current = get_current_process();

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
  
  mode = mode & 0777;
  sc = vfs_mkdir(dvnode, ld.last_component, current->uid, current->gid, mode);

  if (sc != 0) {
    lookup_cleanup(&ld);
    return sc;
  }    

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
  
  Info("sys_rmdir()");

  if ((sc = lookup(_path, LOOKUP_REMOVE, &ld)) != 0) {
    return sc;
  }
  
  vnode = ld.vnode;
  dvnode = ld.parent;

  if (!S_ISDIR(vnode->mode)) {
    lookup_cleanup(&ld);
    return -ENOTDIR;
  }


  // FIXME: vnode may be discarded by vfs_rmdir() or we need to return an error code
  // if it was discarded?
  
  rwlock_shared(&vnode->lock);
  sc = vfs_rmdir(dvnode, vnode, ld.last_component);   // Remove directory name from parent directory 
  rwlock_release(&vnode->lock);
  
  lookup_cleanup(&ld);                                // VNode is discarded if link count is 0.

  return sc;
}


/* @brief   Close a directory and perform any special-case handling
 *
 */
int do_close_dir(struct VNode *vnode)
{
  vnode_put(vnode);     // TODO: Drain lock inside vnode_put?
  return 0;
}


