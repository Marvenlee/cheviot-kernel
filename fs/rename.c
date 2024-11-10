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
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <poll.h>


/* @brief   Rename a file or directory
 *
 * @param   oldpath, the path of the file or directory to rename
 * @param   newpath, the path to rename the file or directory to
 * @return  0 on success, negative errno on failure.
 */
int sys_rename(char *oldpath, char *newpath)
{
  struct lookupdata oldl;
  struct lookupdata newl;
  struct VNode *super_dvnode;
  struct VNode *next_super_dvnode;
  int sc;
  int depth;
  
  if ((sc = lookup(oldpath, LOOKUP_REMOVE, &oldl)) != 0) {
    return sc;
  }

  if (is_mountpoint(oldl.vnode)) {
	  vnode_put(oldl.vnode);
	  vnode_put(oldl.parent);
	  return -EBUSY;
  }

  // TODO: check if sticky bit is set, only owner or root is allowed to rename
  
  if ((sc = lookup(newpath, LOOKUP_PARENT, &newl)) != 0) {
    vnode_put(oldl.vnode);
    vnode_put(oldl.parent);
    return sc;
  }

  // Fail if vnode of new path already exists
  if (newl.vnode != NULL) {
    vnode_put(oldl.vnode);
    vnode_put(oldl.parent);
    vnode_put(newl.parent);
    vnode_put(newl.vnode);
    return -EEXIST;
  }  

  // Fail if not on the same device
  if (newl.parent->superblock != oldl.vnode->superblock) {
    vnode_put(oldl.vnode);
    vnode_put(oldl.parent);
    vnode_put(newl.parent);
    return -EXDEV;
  }

	/* If we're renaming a directory, the old directory must not be a super-directory
	 * of the new directory.
	 */  	 
	if (S_ISDIR(oldl.vnode->mode) && (oldl.parent != newl.parent)) {
    super_dvnode = newl.parent;
    vnode_inc_ref(super_dvnode);    

    sc = 0;
    depth = 0;
    
		while (depth < MAX_RENAME_PATH_CHECK_DEPTH) {
			if (super_dvnode == oldl.vnode) {
				vnode_put(super_dvnode);
				sc = -EINVAL;
				break;
			}

			next_super_dvnode = path_advance(super_dvnode, "..");
			vnode_put(super_dvnode);

      if (next_super_dvnode == NULL) {
				sc = 0;
				break;	      
      }

      // Check if we are root, ".." points to itself
			if(next_super_dvnode == super_dvnode) {
				vnode_put(next_super_dvnode);
				sc = 0;
				break;	
			}

      // Check if we are a mount point
			if(is_mountpoint(next_super_dvnode->vnode_covered)) {
				vnode_put(next_super_dvnode);
				sc = 0;
				break;
			}

			super_dvnode = next_super_dvnode;			
			depth++;
		}
		
		if (depth >= MAX_RENAME_PATH_CHECK_DEPTH) {
		  sc = -ELOOP;
		}
    
    if (newl.parent->nlink >= LINK_MAX) {
      sc = -EMLINK;
    }    
	
	  if (sc != 0) {
      vnode_put(oldl.vnode);
      vnode_put(oldl.parent);
      vnode_put(newl.parent);
      return sc;
	  }
	}
	
  vn_lock(oldl.vnode, VL_UPGRADE);

  // If same dvnode, ensure only a single exclusive lock is held.
  // FIXME: I'm sure it's possible to deadlock this with multiple rename commands.
  
  if (oldl.parent == newl.parent) {
    vn_lock(newl.parent, VL_RELEASE);
    vn_lock(oldl.parent, VL_UPGRADE);
  } else {
    vn_lock(newl.parent, VL_UPGRADE);
    vn_lock(oldl.parent, VL_UPGRADE);
  }
  
  sc = vfs_rename(oldl.parent, oldl.last_component, newl.parent, newl.last_component);

  if (oldl.parent == newl.parent) {
    vn_lock(oldl.parent, VL_RELEASE);
  } else {
    vn_lock(newl.parent, VL_RELEASE);
    vn_lock(oldl.parent, VL_RELEASE);
  }

  vn_lock(oldl.vnode, VL_RELEASE);

  vnode_put(oldl.vnode);
  vnode_put(oldl.parent);
  vnode_put(newl.parent);
  return sc;
}

