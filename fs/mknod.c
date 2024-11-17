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
#include <sys/privileges.h>


/* @brief   Create a node on a file system that can be mounted onto
 *
 * TODO: Move this out into mknod.c.  Allow creation of named pipes/sockets.
 * Change flags to mode, change _stat to dev_t
 */ 
int sys_mknod2(char *_path, uint32_t flags, struct stat *_stat)
{
  struct lookupdata ld;
  struct stat stat;
  int sc;  
  struct VNode *vnode = NULL;

  Info("sys_mknod");

  if (CopyIn(&stat, _stat, sizeof stat) != 0) {
    return -EFAULT;
  }

  if ((sc = lookup(_path, LOOKUP_PARENT, &ld)) != 0) {
    return sc;
  }

  if (ld.vnode != NULL) {    
    vnode_put(ld.vnode);
    vnode_put(ld.parent);
    lookup_cleanup(&ld);
    return -EEXIST;
  }
    
  sc = vfs_mknod(ld.parent, ld.last_component, &stat, &vnode);

  vnode_put(vnode);
  vnode_put(ld.parent);
  lookup_cleanup(&ld);

  return sc;
}

