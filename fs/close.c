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
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>

#define KLOG_GROUP(LOG_FS_CLOSE)

/* @brief   close system call
 *
 */
int sys_close(int fd)
{
  struct Process *current;
  
  klog_info("sys_close(fd:%d)", fd);
  
  current = get_current_process();
  return do_close(current, fd);
}


/* @brief   close a file descriptor in a specific process
 */
int do_close(struct Process *proc, int fd)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct SuperBlock *sb;
  mode_t mode;
  int sc = 0;
  
  klog_info("do_close(fd:%d)", fd);

  if (fd < 0 || fd >= FILEDESC_MAX) {   // TODO:  Add filedesc_find, if null return -einval
    klog_error("do_close() fd out of range: %d, -EINVAL", fd);
    return -EINVAL;
  }

  filp = filp_get(proc, fd);  // TODO: This does bounds checking too. can we return filedesc too?
                              // TODO: Or should be replace fd with pointer to filedesc?  
  
  // TODO: Check if filp or filedesc is busy, return -EAGAIN if so (both?)
  
  
  if (filp) {
    fd_invalidate(proc, fd);

    switch (filp->type) {
      case FILP_TYPE_VNODE: {
        vnode = vnode_get_from_filp(filp);

        fd_free(proc, fd);

        if (filp_release(filp) == 0) {
          kassert(vnode != NULL);
          
          mode = vnode->mode;
           
          if (S_ISREG(mode)) {
            do_close_file(vnode);
          } else if (S_ISDIR(mode)) {
            do_close_dir(vnode);
          } else if (S_ISCHR(mode)) {
            do_close_char_device(vnode);
          } else if (S_ISBLK(mode)) {
            do_close_block_device(vnode);
          } else if (S_ISFIFO(mode)) {
            do_close_pipe(vnode, (filp->flags & O_WRONLY));          
          } else if (S_ISSOCK(mode)) {
          }
        }
        
        break;
      }
      
      case FILP_TYPE_SUPERBLOCK:
        sb = filp->u.superblock;

        fd_free(proc, fd);

        if (filp_release(filp) == 0) {        
          do_close_superblock(sb);
        }
        
        break;
      default:
        kernelpanic();
    }    
  } else {
    sc = -EBADF;
  }

  return sc;
}


