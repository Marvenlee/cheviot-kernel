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

//#define KDEBUG 1

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/proc.h>
#include <kernel/types.h>

/* @brief   Create a symbolic link in the file system
 *
 */
int sys_symlink(char *_path, char *_link)
{
  struct lookupdata ld;
  int sc;

  if ((sc = lookup(_path, LOOKUP_PARENT, &ld)) != 0) {
    return sc;
  }

  if (ld.vnode != NULL) {
    lookup_cleanup(&ld);
    return -EEXIST;
  }

  rwlock(&ld.parent->lock, LK_EXCLUSIVE);

  // TODO:  sc = vfs_mklink(ld.parent, ld.last_component, _link);

  // knote that directory has changed

  rwlock(&ld.parent->lock, LK_RELEASE);

  lookup_cleanup(&ld);
  return 0;
}


/* @brief   Get the path a symbolic link points to
 *
 */
int sys_readlink(char *_path, char *_link, size_t link_size)
{
  struct lookupdata ld;
  int status;
  
  if ((status = lookup(_path, 0, &ld)) != 0) {
    return status;
  }

  if (ld.vnode == NULL) {
    lookup_cleanup(&ld);
    return -EEXIST;
  }

  // TODO, check if vnode is a symlink.
  if (!S_ISLNK(ld.vnode->mode)) {
    lookup_cleanup(&ld);
    return -ENOLINK;
  }

  rwlock(&ld.parent->lock, LK_SHARED);

  // TODO:   status = vfs_readlink(ld.vnode, _link, link_size);

  rwlock(&ld.parent->lock, LK_RELEASE);

  lookup_cleanup(&ld);
  return status;
}

