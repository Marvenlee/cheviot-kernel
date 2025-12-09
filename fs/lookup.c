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
 * File system pathname lookup and conversion to vnodes.
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <string.h>


/* @brief Vnode lookup of a path
 *
 * @param _path
 * @param flags    0 - Lookup vnode of file or dir, do not return parent
 *                 LOOKUP_PARENT  - return parent and optionally vnode if it exists
 *                 LOOKUP_REMOVE - return parent AND vnode
 *                 LOOKUP_NOFOLLOW - Do not follow the last component if a symlink (is this in conjunction with PARENT?
 * @param lookup
 * @returns 0 on success, negative errno on error
 *
 * FIXME: Do we call vfs_close when we vnode_put an inode that has zero reference count?
 */
int lookup(char *_path, int flags, struct lookupdata *ld)
{
  int rc;
  
  klog_info("lookup()");
  
  if ((rc = init_lookup(_path, flags, ld)) != 0) {
    klog_error("Lookup init failed");
    return rc;
  }

  if (flags & LOOKUP_PARENT) {    
    if (ld->path[0] == '/' && ld->path[1] == '\0') {  // Replace with IsPathRoot()
      klog_error("Lookup failed root");
      return -EINVAL;  
    }

    if ((rc = lookup_path(ld)) != 0) {
      klog_error("Lookup failed");
      return rc;
    }

    ld->parent = ld->vnode;
    ld->vnode = NULL;
    
    rc = lookup_last_component(ld);

    // What if it failed for some other reason than it doesn't exit, could this be a vulnerability?    
    // What if it is path/. ?
    // Need to throw an error, we are looking for a path and a last component, path/. is not valid
    return 0;
  
  } else if (flags & LOOKUP_REMOVE) {
    klog_error("Lookup remove failed");
    return -ENOTSUP;        
  } else {
    if (ld->path[0] == '/' && ld->path[1] == '\0') { // Replace with IsPathRoot()
      Info ("lookup \"/\"");

      ld->parent = NULL;
      ld->vnode = root_vnode;
      vnode_ref(ld->vnode);
      return 0;
    }

    if ((rc = lookup_path(ld)) != 0) {
      klog_error("lookup_path rc:%d", rc);
      return rc;
    }
          
    ld->parent = ld->vnode;
    ld->vnode = NULL;
    
    kassert(ld->parent != NULL);
                
    rc = lookup_last_component(ld);

//    if (ld->parent != ld->vnode)            
//    {
      klog_info("lookup() vnode_put of ld->parent before returning");
      vnode_put(ld->parent);
//    }
  
    ld->parent = NULL;   // FIXME: Added 22 sept MG
    
    Info ("lookup rc=%d", rc);
    return rc;
  }
}

/*
 * TODO: lookup_cleanup - add to dir operations and others
 */
void lookup_cleanup(struct lookupdata *ld)
{
  klog_info("lookup_cleanup()");
  
  if (ld->path != NULL) {
    kfree_page(ld->path);
    ld->path = NULL;
    klog_info("..path freed");
  }
  
  if (ld->vnode != NULL) {
    klog_info("..lookup_cleanup vnode:%08x", (uint32_t)ld->vnode);
    vnode_put(ld->vnode);
    ld->vnode = NULL;
  }

  if (ld->parent != NULL) {
    klog_info("..lookup_cleanup parent:%08x", (uint32_t)ld->parent);
    vnode_put(ld->parent);
    ld->parent = NULL;
  }

  ld->last_component = NULL;
  ld->position = NULL;
  ld->start_vnode = NULL;     // FIXME: Do we need to dereference it? and reference it in init_lookup?
}


/* @brief Initialize the state for performing a pathname lookup
 *
 * @param _path
 * @param flags
 * @param lookup
 * @return 0 on success, negative errno on error
 */
int init_lookup(char *_path, uint32_t flags, struct lookupdata *ld)
{
  struct Process *current;
  int path_len;
  
  klog_info("init_lookup");
  
  current = get_current_process();
  
  ld->vnode = NULL;
  ld->parent = NULL;
  ld->position = NULL;
  ld->last_component = NULL;
  ld->flags = flags;
  
  ld->path = kmalloc_page();
  
  klog_info("init_lookup ld->path buf:%08x", (uint32_t)ld->path);
  
  if (ld->path == NULL) {
    klog_error("init_lookup() - Failed to alloc page for pathname");
    return -1;
  }
  
  ld->path[0] = '\0';  

  if (flags & LOOKUP_KERNEL) {
    StrLCpy(ld->path, _path, sizeof ld->path);
  } else if (copyinstring(ld->path, _path, PAGE_SIZE) == -1) {
    klog_error("init_lookup -EFAULT");
    klog_error("ld->path:%08x, _path:%08x", (uint32_t)ld->path, (uint32_t)_path);
    kfree_page(ld->path);
    ld->path = NULL;   
    return -EFAULT; // FIXME:  Could be ENAMETOOLONG 
  }

  path_len = StrLen(ld->path);

  klog_info("init_lookup, path:%s", ld->path);

  // Remove any trailing separators
  
  for (size_t i = path_len; i > 0 && ld->path[i] == '/'; i--) {
    ld->path[i] = '\0';
  }
  
  // FIXME: Do we need to increment reference count of ld->start_vnode ?
  
  ld->start_vnode = (ld->path[0] == '/') ? root_vnode : current->fproc.current_dir;    

  Info ("ld_start_vnode = %08x", (uint32_t)ld->start_vnode);


  kassert(ld->start_vnode != NULL);

  if (ld->start_vnode == NULL) {
    klog_error("Process has no root or current dir to search from");
    kfree_page(ld->path);
    return -EIO;
  }

  vnode_ref(ld->start_vnode);

  if (!S_ISDIR(ld->start_vnode->mode)) {
    klog_error("init_lookup start vnode -ENOTDIR");
    kfree_page(ld->path);
    return -ENOTDIR;
  }

  ld->position = ld->path;
  return 0;
}


/* @brief Lookup the path to the second last component
 *
 * @param lookup - Lookup state
 * @return 0 on success, negative errno on error
 */
int lookup_path(struct lookupdata *ld)
{
  int rc;
  
  Info ("lookup_path");
  
  kassert(ld->start_vnode != NULL);

  ld->parent = NULL;
  ld->vnode = ld->start_vnode;

  vnode_ref(ld->vnode);
  
  while(1) {    
    ld->last_component = path_token(ld);


    if (ld->last_component == NULL) {
      klog_error("lookup_path last_component NULL");
      rc = -EINVAL;
      break;
    }

    Info ("lookup_path last_component:%s", ld->last_component);
    
    if (ld->parent != NULL) {
      klog_info("lookup_path A vnode_put ld->parent %08x", (uint32_t)ld->parent);
    
      vnode_put(ld->parent);
      ld->parent = NULL;
    }  
    
    if (is_last_component(ld)) {
      rc = 0;
      break;
    }    
                    
    ld->parent = ld->vnode;
    ld->vnode = NULL;    
    
    rc = walk_component(ld);

    if (rc != 0) {
      klog_info("lookup_path B vnode_put ld->parent %08x", (uint32_t)ld->parent);

      vnode_put(ld->parent);
      ld->parent = NULL;
      break;
    }
    
    // TODO: Check component is a directory, check permissions
  }

  Info ("lookup_path rc=%d", rc);     
  return rc;
}


/* @brief Lookup the last component of a pathname
 *
 */
int lookup_last_component(struct lookupdata *ld)
{
  int rc;
 
  klog_info("lookup_last_component");
 
  kassert(ld->parent != NULL);
    
  if (ld->last_component == NULL) {
    return -ENOENT;
  }

  if ((rc = walk_component(ld)) != 0) {
    return rc;
  }
  
  return 0;
}


/* @brief Tokenize the next pathname component, 
 *
 * @param lookup - Lookup state
 * @return - Pathname component null terminated string
 */
char *path_token(struct lookupdata *ld)
{
  char *ch;
  char *name;

  Info ("path_token");

  ch = ld->position;
  
  while (*ch == '/') {
    ch++;
  }

  if (*ch == '\0') {
    ld->position = ch;
    ld->separator = '\0';
    
    return NULL;
  }

  name = ch;

  while (*ch != '/' && *ch != '\0') {
    ch++;
  }

  if (*ch == '/') {
    ld->position = ch + 1;
    ld->separator = '/';
  } else {
    ld->position = ch;
    ld->separator = '\0';  
  }

  *ch = '\0';
  
  Info ("path_token retval: %s", name);
  return name;
}


/* @brief Determine if it has reached the last pathname component
 *
 * Check only trailing / characters or null terminator after vnode component
 * 
 * PathToken to store last character replaced, either '\0 or '/'
 *
 * @param lookup - Lookup state
 * @return true if this is the last component, false otherwise
 */
bool is_last_component(struct lookupdata *ld)
{
//  char *ch;
  
  if (ld->separator == '\0') {
    return true;
  }
  
//  for (ch = ld->position; *ch == '/'; ch++) {
//  }
  
  return (*ld->position == '\0');
  
/*
  {
    return true;
  } else {
    return false;
  }
*/

}


/* @brief Walk the vnode component
 *
 * Update lock on new component, release lock on old component
 *
 * @param lookup - Lookup state
 * @param name - Filename to lookup
 * @return 
 */
int walk_component(struct lookupdata *ld)
{
  struct VNode *vnode_mounted_here;
  struct VNode *vnode_tmp;
  int rc;

  klog_info("walk_component()");

  kassert(ld != NULL);
  kassert(ld->parent != NULL);
  kassert(ld->vnode == NULL);
  kassert(ld->last_component != NULL);
  
  if (!S_ISDIR(ld->parent->mode)) {
    klog_error("ld->parent is not a directory");
    return -ENOTDIR;
 
  } else if (StrCmp(ld->last_component, ".") == 0) {    
    klog_info("walk_comp - last comp is .");    
    ld->vnode = ld->parent;
    vnode_ref(ld->vnode);    
    return 0;
  
  } else if (StrCmp(ld->last_component, "..") == 0) {
    klog_info("walk_comp - last comp is ..");    

    if (ld->parent == root_vnode) {
      ld->vnode = root_vnode;
      vnode_ref(root_vnode);
      return 0;
 
    } else if (ld->parent->vnode_covered != NULL) {
      vnode_tmp = ld->parent->vnode_covered; 
      vnode_ref(vnode_tmp);
  
      klog_info("walk_comp - vnode_put Z parent:%08x", (uint32_t)ld->parent);
      vnode_put(ld->parent);
      ld->parent = vnode_tmp;
    }
  }
  
  kassert(ld->parent != NULL);

  // TODO: Do we need dvnode->lock (exclusive) when doing lookup of directory inode?
  // TODO: Dow we lock the new inode too?

  if ((rc = vfs_lookup(ld->parent, ld->last_component, &ld->vnode)) != 0) {
    klog_info("last_component vfs_lookup rc:%d", rc);
    return rc;
  }

  vnode_mounted_here = ld->vnode->vnode_mounted_here;

  // TODO: Check permissions/access here

  if (vnode_mounted_here != NULL) {
      klog_info("lookup vnode mounted here");
    if (is_last_component(ld) == true && (ld->flags & LOOKUP_NOFOLLOW) == 0) {
      // Special-case handling of /dev/tty
      
      struct SuperBlock *sb = vnode_mounted_here->superblock;

      klog_info("lookup is last component and follow");
      klog_info("lookup sb->dev = %08x", sb->dev);
      klog_info("lookup vnode here->mode = %o", vnode_mounted_here->mode);
      klog_info("ld->vnode->mode = %o", ld->vnode->mode);

      if (sb->dev == DEV_T_DEV_TTY && S_ISCHR(vnode_mounted_here->mode)) {        
        klog_info("special case lookup for /dev/tty");
        
        struct Process *current = get_current_process();
      	struct Session *current_session = get_session(current->sid);

      	if (current_session != NULL) {
          vnode_mounted_here = current_session->controlling_tty;
        } else {
          vnode_mounted_here = NULL;
        }

        if (vnode_mounted_here == NULL) {
          klog_warn("vnode_mounted_here is null, vnode_put A vnode:%08x", (uint32_t)ld->vnode);
          vnode_put(ld->vnode);
          ld->vnode = NULL;
          return -EPERM;
        }
      }

      klog_info("walk_comp vnode_put B ld->vnode %08x", (uint32_t)ld->vnode);

      vnode_put(ld->vnode);
      ld->vnode = vnode_mounted_here;            
      vnode_ref(ld->vnode);
      
    } else {
      klog_info("walk_comp vnode_put C ld->vnode %08x", (uint32_t)ld->vnode);

      vnode_put(ld->vnode);
      ld->vnode = vnode_mounted_here;            
      vnode_ref(ld->vnode);
    }    
  }  

  return 0;
}


/*
 * FIXME: Needed for rename directory climb
 */
struct VNode *path_advance(struct VNode *dvnode, char *component)
{
  struct VNode *rvnode;
  
  if ((vfs_lookup(dvnode, component, &rvnode)) != 0) {
    return NULL;
  }
  
  return rvnode;
}

