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
#include <fcntl.h>
#include <kernel/kqueue.h>


/* @brief   The access() system call
 * 
 */
int sys_access(char *pathname, mode_t amode)
{
#if 1
	// FIXME: Enable sys_access - remove return 0
  return 0;
#endif

  struct Process *current;
  struct lookupdata ld;
  struct VNode *vnode;
  int sc;

  current = get_current_process();

  if ((sc = lookup(pathname, 0, &ld)) != 0) {
    return sc;
  }

  vnode = ld.vnode;

  sc = check_access(vnode, NULL, amode);

  knote(&vnode->knote_list, NOTE_ATTRIB);
  lookup_cleanup(&ld);

  return sc;
}


/* @brief   umask system call
 *
 */
mode_t sys_umask (mode_t mode)
{
  mode_t old_mode;
  struct Process *current;
  
  current = get_current_process();
  
  old_mode = current->fproc->umask;
  current->fproc->umask = mode;
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

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) != 0) {
    return sc;
  }

  vnode = ld.vnode;

  rwlock(&vnode->lock, LK_EXCLUSIVE);

  if (vnode->uid == current->uid || current->uid == SUPERUSER) {
    sc = vfs_chmod(vnode, mode);
    
    if (sc == 0) {
      vnode->mode = mode;
    }
  } else {
    Warn("chmod -EPERM");
    sc = EPERM;
  }

  rwlock(&vnode->lock, LK_RELEASE);

  knote(&vnode->knote_list, NOTE_ATTRIB);  
  lookup_cleanup(&ld);
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

  current = get_current_process();

  if ((sc = lookup(_path, 0, &ld)) != 0) {
    return sc;
  }

  vnode = ld.vnode;

  rwlock(&vnode->lock, LK_EXCLUSIVE);

  if (vnode->uid == current->euid || current->euid == SUPERUSER) {
    sc = vfs_chown(vnode, uid, gid);
    
    if (sc == 0) {
      vnode->uid = uid;
      vnode->gid = gid;
    }    
  } else {
    Warn("chown -EPERM");
    sc = -EPERM;
  }

  rwlock(&vnode->lock, LK_RELEASE);

  knote(&vnode->knote_list, NOTE_ATTRIB);
  lookup_cleanup(&ld);
  return 0;
}


/* @brief 	fchmod system call
 *
 */
int sys_fchmod(int fd, mode_t mode)
{
  struct Process *current;
  struct VNode *vnode;
  int sc;

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  rwlock(&vnode->lock, LK_EXCLUSIVE);

  if (vnode->uid == current->euid || current->euid == SUPERUSER) {
    sc = vfs_chmod(vnode, mode);
    
    if (sc == 0) {
      vnode->mode = mode;
    }
  } else {
    Warn("fchmod -EPERM");
    sc = EPERM;
  }

  rwlock(&vnode->lock, LK_RELEASE);

  knote(&vnode->knote_list, NOTE_ATTRIB);
  
  vnode_put(vnode);
  return sc;
}


/* @brief   fchown system call
 *
 * FIXME: does get_fd_vnode lock the vnode?
 */
int sys_fchown(int fd, uid_t uid, gid_t gid)
{
  struct Process *current;
  struct VNode *vnode;
  int sc;

  current = get_current_process();
  vnode = get_fd_vnode(current, fd);

  if (vnode == NULL) {
    return -EINVAL;
  }

  rwlock(&vnode->lock, LK_EXCLUSIVE);

  if (vnode->uid == current->euid || current->euid == SUPERUSER) {
    sc = vfs_chown(vnode, uid, gid);
    
    if (sc == 0) {
      vnode->uid = uid;
      vnode->gid = gid;
    }    
  } else {
    Warn("fchown -EPERM");
    sc = EPERM;
  }

  rwlock(&vnode->lock, LK_RELEASE);

  knote(&vnode->knote_list, NOTE_ATTRIB);
  vnode_put(vnode);
  return 0;
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
  
  current = get_current_process();
  
#if 1
  return 0;
#endif
  
  // FIXME: check_access
  
  if (current->euid == SUPERUSER) {
    return 0;
  }
  
  desired_access &= (R_OK | W_OK | X_OK);

  if ((desired_access & W_OK) && (vnode->superblock->flags & SF_READONLY)) {
    return -EPERM;
  }

  if (filp != NULL) {
    int access_mode = (filp->flags & O_ACCMODE);
    
    if ((access_mode == O_RDONLY && (desired_access & W_OK)) ||
        (access_mode == O_WRONLY && (desired_access & R_OK))) {
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




