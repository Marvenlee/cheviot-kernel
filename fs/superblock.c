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


/*
 * Get the superblock of a file descriptor created by sys_mount()
 */
struct SuperBlock *get_superblock(struct Process *proc, int fd)
{
  struct Filp *filp;

  KASSERT(proc != NULL);
  
  filp = get_filp(proc, fd);
    
  if (filp == NULL) {
    Error("get_superblock, filp is NULL");
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_SUPERBLOCK) {
    Error("get_superblock, filp type is not SUPERBLOCK");
    return NULL;
  }
    
  return filp->u.superblock;
}


/*
 * Allocates a handle structure.  Checks to see that free_handle_cnt is
 * non-zero should already have been performed prior to calling alloc_fd().
 */
int alloc_fd_superblock(struct Process *proc)
{
  int fd;
  struct SuperBlock *sb;

  fd = alloc_fd_filp(proc);
  
  if (fd < 0) {
    Error("alloc_fd_superblock fd < 0");
    return -EMFILE;
  }
  
  sb = alloc_superblock();
  
  if (sb == NULL) {
    free_fd_filp(proc, fd);
    return -EMFILE;
  }
 
  set_fd(proc, fd, FILP_TYPE_SUPERBLOCK, 0, sb);  
  return fd;
}


/*
 * Returns a handle to the free handle list.
 */
int free_fd_superblock(struct Process *proc, int fd)
{
  struct SuperBlock *sb;
  
  sb = get_superblock(proc, fd);

  if (sb == NULL) {
    return -EINVAL;
  }

  free_superblock(sb);
  free_fd_filp(proc, fd);
  return 0;
}


/*
 *
 */
struct SuperBlock *alloc_superblock(void)
{
  struct SuperBlock *sb;

  sb = LIST_HEAD(&free_superblock_list);

  if (sb == NULL) {
    Error("no free superblocks");
    return NULL;
  }

  sb->reference_cnt = 1;

  LIST_REM_HEAD(&free_superblock_list, link);
  memset(sb, 0, sizeof *sb);
  InitRendez (&sb->rendez);
  
  sb->dev = 0xdead;
  return sb;
}


/*
 *
 */
void free_superblock(struct SuperBlock *sb)
{
  KASSERT (sb != NULL);

  sb->reference_cnt--;
  
  if (sb->reference_cnt == 0) {
    // TODO: Wakeup anything block on rendez ?
    LIST_ADD_TAIL(&free_superblock_list, sb, link);
  }
}


