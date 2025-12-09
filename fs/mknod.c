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

#define KLOG_GROUP(LOG_FS_MKNOD)


/* @brief   Create a node on a file system that can be mounted onto
 *
 * TODO: Allow creation of named pipes/sockets.
 *       Change flags to mode, change _stat to dev_t
 */ 
int sys_mknod2(char *_path, mode_t mode)
{
  struct lookupdata ld;
  struct Process *current;
  int sc;
  
  current = get_current_process();

  if ((sc = lookup(_path, LOOKUP_PARENT, &ld)) != 0) {
    return sc;
  }

  if (ld.vnode != NULL) {    
    lookup_cleanup(&ld);
    return -EEXIST;
  }


  // TODO: Check if mode is char or dev
  
  sc = vfs_mknod(ld.parent, ld.last_component, current->uid, current->gid, mode);

  if (sc != 0) {
    klog_error("vfs_mknod() failed, sc:%d", sc);
    lookup_cleanup(&ld);
    return -ENOMEM;  
  }

  lookup_cleanup(&ld);
  return sc;
}

