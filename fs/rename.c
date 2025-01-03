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
#include <sys/privileges.h>


/* @brief   Rename a file or directory
 *
 * @param   oldpath, the path of the file or directory to rename
 * @param   newpath, the path to rename the file or directory to
 * @return  0 on success, negative errno on failure.
 *
 * TODO: Need to lookup both old and new directories.
 * Need to lookup old last component vnode.
 * Neeed to lookup old last component vnode in new directory
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
    lookup_cleanup(&oldl);
	  return -EBUSY;
  }

  // TODO: check if sticky bit is set, only owner or root is allowed to rename
  
  if ((sc = lookup(newpath, LOOKUP_PARENT, &newl)) != 0) {
    lookup_cleanup(&oldl);
    return sc;
  }

  // Fail if vnode of new path already exists
  if (newl.vnode != NULL) {
    lookup_cleanup(&oldl);
    lookup_cleanup(&newl);
    return -EEXIST;
  }  

  // Fail if not on the same device
  if (newl.parent->superblock != oldl.vnode->superblock) {
    lookup_cleanup(&oldl);
    lookup_cleanup(&newl);
    return -EXDEV;
  }

	/* If we're renaming a directory, the old directory must not be a super-directory
	 * of the new directory.
	 */  	 
	if (S_ISDIR(oldl.vnode->mode) && (oldl.parent != newl.parent)) {
    super_dvnode = newl.parent;
    vnode_add_reference(super_dvnode);    

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
	  
	  // FIXME: Do we need to vnode_put() super_dvnode or next_super_dvnode ?
	  
	  if (sc != 0) {
      lookup_cleanup(&oldl);
      lookup_cleanup(&newl);
      return sc;
	  }
	}
	
  rwlock(&oldl.vnode, LK_UPGRADE);

  // If same dvnode, ensure only a single exclusive lock is held.
  // FIXME: I'm sure it's possible to deadlock this with multiple rename commands.
  // Do we need to lock the vnodes in kernel address order ?
    
  if (oldl.parent == newl.parent) {
    rwlock(&newl.parent->lock, LK_RELEASE);
    rwlock(&oldl.parent->lock, LK_UPGRADE);
  } else {
    rwlock(&newl.parent->lock, LK_UPGRADE);
    rwlock(&oldl.parent->lock, LK_UPGRADE);
  }
  
  sc = vfs_rename(oldl.parent, oldl.last_component, newl.parent, newl.last_component);

  if (oldl.parent == newl.parent) {
    rwlock(&oldl.parent->lock, LK_RELEASE);
  } else {
    rwlock(&newl.parent->lock, LK_RELEASE);
    rwlock(&oldl.parent->lock, LK_RELEASE);
  }

  rwlock(&oldl.vnode->lock, LK_RELEASE);
  
  lookup_cleanup(&oldl);
  lookup_cleanup(&newl);
  return sc;
}

