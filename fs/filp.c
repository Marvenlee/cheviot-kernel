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
 *
 * --
 * File pointer handling.
 *
 * A file pointer (filp) is an intermediate structure between an integer file
 * descriptor and a vnode. It can be shared by multiple file descriptors from
 * the same or multiple processes.  It contains the current seek position and
 * file oflags access type.
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>


/* @brief   Lookup a file pointer from a file descriptor
 *
 * @param   proc, process that the file descriptor belongs to
 * @param   fd, file descriptor to look up
 * @return  filp (file pointer) structure
 */
struct Filp *filp_get(struct Process *proc, int fd)
{
  struct Filp *filp;
  
  if (fd < 0 || fd >= OPEN_MAX) {
    Info("filp_get, fd:%d out of range", fd);
    return NULL;
  }

  if ((proc->fproc.fd_table[fd].flags & FDF_VALID) == 0) {
    Info("get_filp() fd:%d, not valid", fd);
    return NULL;
  }
  
  if (proc->fproc.fd_table[fd].filp == NULL) {
    Error("get_filp() fd:%d, no filp", fd);
    return NULL;
  }
  
  filp = proc->fproc.fd_table[fd].filp;
  
  filp->reference_cnt++;

  while(filp->busy == true) {
    TaskSleep(&filp->rendez);
  }
  
//  filp->busy = true;
  
  return filp;
}


/*
 *
 */
void filp_put(struct Filp *filp)
{
  filp->reference_cnt--;
//  filp->busy = false;
  TaskWakeup(&filp->rendez);
}
 

/* @brief   Allocate a filp (file pointer)
 */
struct Filp *filp_alloc(void)
{
  struct Filp *filp;

  filp = LIST_HEAD(&filp_free_list);

  if (filp == NULL) {
    return NULL;
  }

  LIST_REM_HEAD(&filp_free_list, filp_entry);
  filp->reference_cnt = 1;
  filp->type = FILP_TYPE_UNDEF;
  memset(&filp->u, 0, sizeof filp->u);
  
  filp->flags = 0;
//  filp->busy = true;
  InitRendez(&filp->rendez);
  
  return filp;
}


/* @brief   Free a filp (file pointer)
 */
void filp_free(struct Filp *filp)
{
  if (filp == NULL) {
    return;
  }

  filp->reference_cnt--;
 
  if (filp->reference_cnt == 0) {
    filp->type = FILP_TYPE_UNDEF;
    memset(&filp->u, 0, sizeof filp->u);
    LIST_ADD_HEAD(&filp_free_list, filp, filp_entry);
  }
}

