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

  Info("sys_unlink()");

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

  rwlock_shared(&dvnode->lock);
  rwlock_shared(&vnode->lock);
  sc = vfs_unlink(dvnode, vnode, ld.last_component);
  rwlock_release(&vnode->lock);
  rwlock_release(&dvnode->lock);
  
  if (sc == 0) {
    ld.vnode = NULL;
  }
  
  lookup_cleanup(&ld);
  return 0;  
}


