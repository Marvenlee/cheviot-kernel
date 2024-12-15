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
 * Mount a server, filesystem handler or device driver message port
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/utility.h>
#include <poll.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/privileges.h>


/* @brief   Pivot the root directory
 *
 * TODO: Check these are directories (ROOT ones)
 * TODO: Acquire old_root first, as this may be subdir of new root
 * and would cause deadlock due to new_root's busy flag being set.
 * Unless we unlock it (but it remains in memory due to ref cnt
 */
int sys_pivotroot(char *_new_root, char *_old_root)
{
  struct lookupdata new_ld, old_ld;
  struct VNode *new_root_vnode;
  struct VNode *old_root_vnode;
  struct VNode *current_root_vnode;
  int sc;
	
  if ((sc = lookup(_old_root, 0, &old_ld)) != 0) {
    Error("PivotRoot lookup _old_root failed");
    return sc;
  }

  old_root_vnode = old_ld.vnode;

  if (old_root_vnode == NULL) {
    Error("PivotRoot lookup _old_root -ENOENT");
    lookup_cleanup(&old_ld);
    return -ENOENT;
  }

  vnode_add_reference(old_root_vnode);

  if ((sc = lookup(_new_root, 0, &new_ld)) != 0) {
    Error("PivotRoot lookup _new_root failed");
    vnode_put(old_root_vnode);
    lookup_cleanup(&old_ld);
    return sc;
  }

  new_root_vnode = new_ld.vnode;

  if (new_root_vnode == NULL) {
    Error("PivotRoot failed new_root -ENOENT");
    vnode_put(old_root_vnode);
    lookup_cleanup(&old_ld);
    lookup_cleanup(&new_ld);
    return -ENOENT;
  }

  vnode_add_reference(new_root_vnode);

  current_root_vnode = root_vnode;
  old_root_vnode->vnode_mounted_here = current_root_vnode;
  current_root_vnode->vnode_covered = old_root_vnode;
  root_vnode = new_root_vnode;
  new_root_vnode->vnode_covered = root_vnode;
  
  vnode_put(old_root_vnode);

  lookup_cleanup(&old_ld);
  lookup_cleanup(&new_ld);
  return 0;
}


/* @brief   Move a mounted object to another location in the filesystem namespace.
 *
 * Used in conjunction with sys_pivotroot to move the /dev mount on the IFS 
 * to /dev on the new root moumt 
 */
int sys_renamemount(char *_new_path, char *_old_path)
{
  struct lookupdata old_ld;
  struct lookupdata new_ld;
  struct VNode *new_vnode;
  struct VNode *old_vnode;
  struct VNode *covered_vnode;
  int sc;
  
  if ((sc = lookup(_new_path, 0, &new_ld)) != 0) {
    Error("Failed to find new path");
    return sc;
  }

  new_vnode = new_ld.vnode;
  
  if (new_vnode->vnode_mounted_here != NULL) {
    Error("new vnode already has mount\n");
    lookup_cleanup(&new_ld);
    return -EEXIST;
  }

  if ((sc = lookup(_old_path, 0, &old_ld)) != 0) {
    Error("Failed to find old path");
    lookup_cleanup(&new_ld);
    return sc;
  }

  old_vnode = old_ld.vnode;
    
  if (old_vnode->vnode_mounted_here == NULL) {
    if (old_vnode->vnode_covered == NULL) {
	    Error("old vnode not a mount point\n");
      lookup_cleanup(&new_ld);
      lookup_cleanup(&old_ld);
      return -EINVAL;
	  }
	  
	  covered_vnode = old_vnode->vnode_covered;

	  vnode_add_reference(covered_vnode);
	  vnode_put(old_vnode);
	  old_vnode = covered_vnode;
  }
  
  // Only support directories?
  // Check if old is a mount covering a vnode, is a root vnode  
  // Check that new is a dir but not covered or covering
    
  new_vnode->vnode_mounted_here = old_vnode->vnode_mounted_here;  
  new_vnode->vnode_mounted_here->vnode_covered = new_vnode;  
  old_vnode->vnode_mounted_here = NULL;
    
  lookup_cleanup(&new_ld);
  lookup_cleanup(&old_ld);
  return 0;  
}


/* @brief   Check if a filesystem path is a mount point.
 *
 * @return  1 if it is a mount point
 *          0 if it is not a mount point
 *          negative errno on failure
 */
int sys_ismount(char *_path)
{
  struct lookupdata ld;
  int sc;  

  Info("sys_ismount");

  if ((sc = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
    Error("sys_ismount lookup failed: %d", sc);
    return sc;
  }

  if (ld.vnode == NULL) {
    Error("sys_ismount, inode not found, -ENOENT");
    lookup_cleanup(&ld);
    return -ENOENT;
  }
  
  sc = is_mountpoint(ld.vnode);

  lookup_cleanup(&ld);  
  return sc;  
}


/* @brief   Check if a vnode is a mount point for a filesystem or device
 *
 * @return  true if it is a mount point, false otherwise
 *
 * This checks if either the vnode is mounted on top of another or if
 * another vnode is mounted on top of this vnode.
 */
bool is_mountpoint(struct VNode *vnode)
{
  if (vnode->vnode_covered != NULL || vnode->vnode_mounted_here != NULL) {
    return true;
  } else {
    return false;
  }
}



