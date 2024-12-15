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
  struct VNode *vnode;
  struct Pipe *pipe;

  Info("do_close(fd:%d)", fd);

  
  filp = get_filp(proc, fd);
  
  if (filp == NULL) {
    return -EINVAL;
  }
  
  filp->reference_cnt--;  
  
  KASSERT(filp->reference_cnt >= 0);
  
  if (filp->reference_cnt == 0) {  
    switch (filp->type) {
        case FILP_TYPE_VNODE:
        close_vnode(proc, fd);
        break;
      case FILP_TYPE_SUPERBLOCK:
        close_msgport(proc, fd);
        break;
      case FILP_TYPE_KQUEUE:
        close_kqueue(proc, fd);
        break;
      default:
        KernelPanic();
    }
  }

  
  free_fd(proc, fd);
  
  return 0;
}


