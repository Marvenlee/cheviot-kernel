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
#include <string.h>


/* @brief   close system call
 *
 */
int sys_close(int fd)
{
  struct Process *current;
  
  Info("sys_close(fd:%d)", fd);
  
  current = get_current_process();
  return do_close(current, fd);
}


/* @brief   close a file descriptor in a specific process
 */
int do_close(struct Process *proc, int fd)
{
  struct Filp *filp;
  
  Info("do_close(fd:%d)", fd);

  if (fd < 0 || fd >= FILEDESC_MAX) {   // TODO:  Add filedesc_find, if null return -einval
    Error("do_close() fd out of range: %d, -EINVAL", fd);
    return -EINVAL;
  }

  filp = filp_get(proc, fd);  // TODO: This does bounds checking too. can we return filedesc too?
                              // TODO: Or should be replace fd with pointer to filedesc?  
  
  if (filp) {
    proc->fproc.fd_table[fd].flags &= ~FDF_VALID;    // Disable access by other user processes to this file descriptor.
    
    filp->reference_cnt--;  // Cancel out the temporary increase due to filp_get
    
    Info("do_close() filp:%08x, ref_cnt:%d", (uint32_t)filp, filp->reference_cnt);
    
    if (filp->reference_cnt == 1) {   // TODO: Mark filp as not duplicable, mark FD as busy/in-use/locked
            
      switch (filp->type) {
          case FILP_TYPE_VNODE:
          Info("call close_vnode");
          close_vnode(filp->u.vnode, filp->flags);
                    
          break;
        case FILP_TYPE_SUPERBLOCK:    // TODO: Change to messageport
          Info("call close_superblock");
          close_superblock(filp->u.superblock);
          break;
        case FILP_TYPE_KQUEUE:
          Info("call close_kqueue");
          close_kqueue(filp->u.kqueue);
          break;
        default:
          KernelPanic();
      }

      filp_free(filp);
    }

    fd_free(proc, fd);
  } else {
//    Info("do_close(fd:%d) - EBADF", fd);
    return -EBADF;
  }

  return 0;
}


