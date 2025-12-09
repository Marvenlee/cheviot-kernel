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
#include <poll.h>
#include <string.h>
#include <fcntl.h>

#define KLOG_GROUP(LOG_FS_ACCESS)

/* @brief   The access() system call
 * 
 */
int sys_access(char *pathname, mode_t amode)
{
  struct lookupdata ld;
  struct VNode *vnode;
  int sc;

  klog_info("sys_access()");

  if ((sc = lookup(pathname, 0, &ld)) != 0) {
    return sc;
  }

  vnode = ld.vnode;

  rwlock_exclusive(&vnode->lock);
  sc = check_access(vnode, NULL, amode);
  rwlock_release(&vnode->lock);

  lookup_cleanup(&ld);

  return sc;
}


/* @brief   umask system call
 *
 */
mode_t sys_umask(mode_t mode)
{
  mode_t old_mode;
  struct Process *current;
  
  current = get_current_process();
  
  old_mode = current->fproc.umask;
  current->fproc.umask = mode;
  return old_mode;
}


/* @brief chmod system call
 *
 */
int sys_chmod(char *_path, mode_t mode)
{
  struct Process *current;
  struct lookupdata ld;
  struct VNode *vnode;
  int sc;

  klog_info("sys_chmod()");

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    vnode = ld.vnode;

    if (vnode->uid == current->uid || current->uid == SUPERUSER) {

      sc = vfs_chmod(vnode, mode);

      if (sc == 0) {
        vnode->mode = mode;
      }
    } else {
      sc = -EPERM;
    }

    lookup_cleanup(&ld);
    return sc;
  }

  return sc;
}


/* @brief   chown system call
 *
 * TODO: Can uid and gid be negative (to ignore?)
 */
int sys_chown(char *_path, uid_t uid, gid_t gid)
{
  struct Process *current;
  struct lookupdata ld;
  struct VNode *vnode;
  int sc;
  
  klog_info("sys_chown()");

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    vnode = ld.vnode;

    if (vnode->uid == current->euid || current->euid == SUPERUSER) {
      rwlock_exclusive(&vnode->lock);
      sc = vfs_chown(vnode, uid, gid);
      rwlock_release(&vnode->lock);

      if (sc == 0) {
        vnode->uid = uid;
        vnode->gid = gid;
      }
    } else {
      sc = -EPERM;
    }

    lookup_cleanup(&ld);
    return 0;
  }

  return sc;
}


/* @brief 	fchmod system call
 *
 */
int sys_fchmod(int fd, mode_t mode)
{
  struct Process *current;
  struct VNode *vnode;
  struct Filp *filp;
  int sc;

  klog_info("sys_fchmod(fd:%d, mod:%0o)", fd, mode);

  current = get_current_process();

  filp = filp_get(current, fd);

  if (filp) {
    vnode = vnode_get_from_filp(filp);
    
    if (vnode) {
      if (vnode->uid == current->euid || current->euid == SUPERUSER) {
        rwlock_exclusive(&vnode->lock);
        sc = vfs_chmod(vnode, mode);
        rwlock_release(&vnode->lock);

        if (sc == 0) {
          vnode->mode = mode;
        }
      } else {
        klog_warn("fchmod -EPERM");
        sc = EPERM;
      }

      return sc;
    }
    
    return -EINVAL;
  }
  
  klog_info("sys_fchmod() - EBADF, fd:%d", fd);
  return -EBADF;
}


/* @brief   fchown system call
 *
 * FIXME: does filp_get lock the vnode?
 */
int sys_fchown(int fd, uid_t uid, gid_t gid)
{
  int sc;
  struct VNode *vnode;
  struct Filp *filp;
  struct Process *current;

  klog_info("sys_fchpwn(fd:%d)", fd);

  current = get_current_process();

  filp = filp_get(current, fd);

  if (filp) {
    vnode = vnode_get_from_filp(filp);

    if (vnode) {
      if (vnode->uid == current->euid || current->euid == SUPERUSER) {
        rwlock_exclusive(&vnode->lock);
        sc = vfs_chown(vnode, uid, gid);
        rwlock_release(&vnode->lock);

        if (sc == 0) {
          vnode->uid = uid;
          vnode->gid = gid;
        }
      } else {
        klog_warn("fchown -EPERM");
        sc = -EPERM;
      }

      return sc;
    }

    return -EINVAL;
  }

  klog_info("sys_fchown() - EBADF, fd:%d", fd);

  return -EBADF;
}


/* @brief   Check if a read/write/execute operation is allowed on a file
 * 
 * @param   vnode, filesystem vnode that we intend to access
 * @param   filp, optional file pointer containing the file access mode.
 *                 O_RDONLY, O_WRONLY or O_RDWR.
 * @param   desired_access, requested file access to check against
 *                 R_OK, W_OK, X_OK.
 * @return  0 on success or -EACESS if the operation is not allowed.                                   
 *
 */
int check_access(struct VNode *vnode, struct Filp *filp, mode_t desired_access)
{
  mode_t perm_bits;
  int shift;
  struct Process *current;
  
//  klog_info("check_access(vnode:%08x, filp:%08x, bits:%0o)",
//                    (uint32_t)vnode, (uint32_t)filp, (uint32_t)desired_access);
  
  current = get_current_process();
  
//  klog_info("current->euid = %d, superuser is %d", current->euid, SUPERUSER);
  
  if (current->euid == SUPERUSER) {
//    klog_info("euid is superuser");
    return 0;
  }
  
  desired_access &= (R_OK | W_OK | X_OK);

  if ((desired_access & W_OK) && (vnode->superblock->flags & SBF_READONLY)) {
    klog_error("access -EPERM desired W_OK && SBF_READLONLY");
    return -EPERM;
  }

  if (filp != NULL) {
    int access_mode = (filp->flags & O_ACCMODE);
    
    if ((access_mode == O_RDONLY && (desired_access & W_OK)) ||
        (access_mode == O_WRONLY && (desired_access & R_OK))) {

      klog_error("access -EPERM desired x_OK but access_mode is either rdonly or wronly");
      return -EPERM;
    }
  }

  if (current->euid == vnode->uid) {
    shift = 6; /* owner */
    perm_bits = (vnode->mode >> shift) & (R_OK | W_OK | X_OK);

    if ((perm_bits & desired_access) == perm_bits) {
      return 0;
    }
  }
  
  if (current->egid == vnode->gid) {
    shift = 3; /* group */
    perm_bits = (vnode->mode >> shift) & (R_OK | W_OK | X_OK);

    if ((perm_bits & desired_access) == perm_bits) {
      return 0;
    }
  }
    
  if (match_supplementary_group(current, vnode->gid) == true) {
    shift = 3; /* supplementary group */
    perm_bits = (vnode->mode >> shift) & (R_OK | W_OK | X_OK);

    if ((perm_bits & desired_access) == perm_bits) {
      return 0;
    }
  }

  shift = 0; /* other */
  perm_bits = (vnode->mode >> shift) & (R_OK | W_OK | X_OK);

  if ((perm_bits | desired_access) == perm_bits) {
    return 0;
  }
  
  Info ("check_access end -EACCES");
  return -EACCES;
}


/*
 *
 */
bool match_supplementary_group(struct Process *proc, gid_t gid)
{
  for (int t=0; t<proc->ngroups; t++) {
    if (proc->groups[t] == gid) {
      return true;
    }
  }
  
  return false;
}




