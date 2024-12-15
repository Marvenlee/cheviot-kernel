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


/*
 *
 */
int sys_unlink(char *_path)
{
  struct VNode *vnode = NULL;
  struct VNode *dvnode = NULL;
  struct lookupdata ld;
  int sc;

  if ((sc = lookup(_path, LOOKUP_REMOVE, &ld)) != 0) {
    return sc;
  }

  vnode = ld.vnode;
  dvnode = ld.parent;

  /* TODO: Could be anything other than dir
   * need to check if it is a mount too.
   */

  if (!S_ISREG(vnode->mode)) {
    lookup_cleanup(&ld);
    return -EINVAL;  // FIXME: possibly ENOTFILE, EISDIR  ??
  }
  
  rwlock(&dvnode->lock, LK_EXCLUSIVE);  // Exclusive lock to remove entries from directory
  rwlock(&vnode->lock, LK_DRAIN);       // Drain lock to remove vnode from directory  

  sc = vfs_unlink(dvnode, vnode, ld.last_component);

  if (sc == 0) {
    ld.vnode = NULL;
  }
  
  knote(&dvnode->knote_list,  NOTE_WRITE | NOTE_ATTRIB);    // Can't knote deleted vnode

  rwlock(&dvnode->lock, LK_RELEASE);
  
  lookup_cleanup(&ld);
  return 0;  
}


