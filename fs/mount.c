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
#include <kernel/vm.h>
#include <kernel/utility.h>
#include <poll.h>
#include <string.h>
#include <sys/mount.h>


/* @brief   Pivot the root directory
 *
 * TODO: Check these are directories (ROOT ones)
 * TODO: Acquire old_root first, as this may be subdir of new root
 * and would cause deadlock due to new_root's busy flag being set.
 * Unless we unlock it (but it remains in memory due to ref cnt
 */
int sys_pivotroot(char *_new_root, char *_old_root)
{
  struct lookupdata ld;
  struct VNode *new_root_vnode;
  struct VNode *old_root_vnode;
  struct VNode *current_root_vnode;
  int sc;

	
  if ((sc = lookup(_old_root, 0, &ld)) != 0) {
    Error("PivotRoot lookup _old_root failed");
    return sc;
  }

  old_root_vnode = ld.vnode;


  if ((sc = lookup(_new_root, 0, &ld)) != 0) {
    Error("PivotRoot lookup _new_root failed");
    return sc;
  }

  new_root_vnode = ld.vnode;

  if (new_root_vnode == NULL) {
    Error("PivotRoot failed new_root -ENOENT");
    return -ENOENT;
  }

  if (old_root_vnode == NULL) {
    Error("PivotRoot lookup _old_root -ENOENT");
    vnode_put(new_root_vnode);
    return -ENOENT;
  }

  current_root_vnode = root_vnode;
  
  old_root_vnode->vnode_mounted_here = current_root_vnode;
  current_root_vnode->vnode_covered = old_root_vnode;

  root_vnode = new_root_vnode;
  new_root_vnode->vnode_covered = root_vnode;

  // TODO: Do we need to do any reference counting tricks, esp for current_vnode?
  
//  dname_purge_all();     FIXME:
  vnode_put (old_root_vnode);
  vnode_put (new_root_vnode);
  return 0;
}


/* @brief   Move a mounted object to another location in the filesystem namespace.
 *
 * Used in conjunction with sys_pivotroot to move the /dev mount on the IFS 
 * to /dev on the new root moumt 
 */
int sys_renamemount(char *_new_path, char *_old_path)
{
  struct lookupdata ld;
  struct VNode *new_vnode;
  struct VNode *old_vnode;
  struct VNode *covered_vnode;
  int error;
  
  if ((error = lookup(_new_path, 0, &ld)) != 0) {
    Error("Failed to find new path");
    goto exit;
  }

  new_vnode = ld.vnode;
  
  if (new_vnode->vnode_mounted_here != NULL) {
    Error("new vnode already has mount\n");
    goto exit;
  }

  if ((error = lookup(_old_path, 0, &ld)) != 0) {
    Error("Failed to find old path");
    goto exit;
  }

  old_vnode = ld.vnode;
    
  if (old_vnode->vnode_mounted_here == NULL) {
    if (old_vnode->vnode_covered == NULL) {
	    Error("old vnode not a mount point\n");
	    goto exit;
	  }
	  
	  covered_vnode = old_vnode->vnode_covered;

	  vnode_inc_ref(covered_vnode);
	  vnode_put(old_vnode);
	  old_vnode = covered_vnode;
  }
  
  // Only support directories?
  // Check if old is a mount covering a vnode, is a root vnode  
  // Check that new is a dir but not covered or covering
    
  new_vnode->vnode_mounted_here = old_vnode->vnode_mounted_here;  
  new_vnode->vnode_mounted_here->vnode_covered = new_vnode;  
  old_vnode->vnode_mounted_here = NULL;
    
//  dname_purge_all();     FIXME:

  vnode_put (old_vnode);   // release 
  vnode_put (new_vnode);   // release
  return 0;
  
exit:
  Error("renamemsgport failed: %d", error);
  return error;
}


/*
 *
 */
int sys_ismount(char *_path)
{
  struct lookupdata ld;
  int sc;  
  struct VNode *vnode = NULL;

  Info("sys_ismount");


  if ((sc = lookup(_path, LOOKUP_NOFOLLOW, &ld)) != 0) {
    Error("sys_ismount lookup failed: %d", sc);
    return sc;
  }

  if (ld.vnode == NULL) {
    Error("sys_ismount, inode not found, -ENOENT");
    return -ENOENT;
  }
  
  vnode = ld.vnode;
  
  if (vnode->vnode_covered != NULL || vnode->vnode_mounted_here != NULL) {
    Info("is mount");
    sc = 1;
  } else {
    Info("not mount");
    sc = 0;
  }

  vnode_put(vnode);
  
  return sc;  
}


/*
 *
 */
int sys_unmount(char *_path, uint32_t flags)
{
  return -ENOSYS;
}


