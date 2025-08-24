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

  filp = filp_get(proc, fd);
  
  if (filp) {
    Info("filp:%08x", (uint32_t)filp);
    Info("filp->reference_cnt = %d", filp->reference_cnt);
    
    if (filp->reference_cnt == 1) {   // TODO: Mark filp as not duplicable, mark FD as busy/in-use/locked
      // TODO:
      switch (filp->type) {
          case FILP_TYPE_VNODE:
          Info("call close_vnode");
          close_vnode(filp->u.vnode, filp->flags);
          break;
        case FILP_TYPE_SUPERBLOCK:
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

      Info("call filp_free()");
      filp_free(filp);
    }

    Info("call fd_free()");
    fd_free(proc, fd);
  } else {
    Info("do_close() -EBADF");
    return -EBADF;
  }

  Info("do_close - success");     

  return 0;
}


