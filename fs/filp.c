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

#define KDEBUG

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
  
  Info("filp_get(proc:%08x, fd:%d)", (uint32_t)proc, fd);
  
  if (fd < 0 || fd >= FILEDESC_MAX) {
    Info("filp_get, fd:%d error out of range", fd);
    return NULL;
  }

  if ((proc->fproc.fd_table[fd].flags & FDF_VALID) == 0) {
    Info("get_filp() fd:%d, error not valid", fd);
    return NULL;
  }
  
  if (proc->fproc.fd_table[fd].filp == NULL) {
    Error("get_filp() fd:%d, error no filp", fd);
    return NULL;
  }
  
  filp = proc->fproc.fd_table[fd].filp;
  
  filp->reference_cnt++;

  while(filp->busy == true) {
    TaskSleep(&filp->rendez);
  }
  
//  filp->busy = true;
  
//  Info("filp_get(fd:%d) => filp: %08x", fd, (uint32_t)filp);
  
  return filp;
}


/*
 *
 */
void filp_put(struct Filp *filp)
{
  filp->reference_cnt--;
//  filp->busy = false;

  Info("filp_put(%08x) after ref_cnt:%d", (uint32_t)filp, filp->reference_cnt);

  TaskWakeup(&filp->rendez);
}
 

/* @brief   Allocate a filp (file pointer)
 */
struct Filp *filp_alloc(void)
{
  struct Filp *filp;

  filp = LIST_HEAD(&filp_free_list);

  if (filp == NULL) {
    Info("filp_alloc() failed, out of filps");
    return NULL;
  }

  LIST_REM_HEAD(&filp_free_list, filp_entry);
  filp->reference_cnt = 1;
  Info("filp_alloc, setting type initially to FILP_TYPE_UNDEF");
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
    Error("filp_free() ERROR, freeing null filp");
    return;
  }

  filp->reference_cnt--;
 
  if (filp->reference_cnt == 0) {
    Info("filp:%08x ref_cnt is 0, setting type to FILP_TYPE_UNDEF");
    filp->type = FILP_TYPE_UNDEF;

    Info("setting filp->u to NULL, adding to free list");
    memset(&filp->u, 0, sizeof filp->u);
    LIST_ADD_HEAD(&filp_free_list, filp, filp_entry);
  }
}


/* @brief   Lookup a vnode from a file pointer
 *
 * @param   filp, file pointer object that points to the vnode
 */
struct VNode *vnode_get_from_filp(struct Filp *filp)
{
  if (filp == NULL) {
    Error("vnode_get_from_filp, filp is NULL");
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_VNODE) {
    Info("vnode_get_from_filp, filp->type is not vnode: %d", filp->type);
    return NULL;
  }
  
  vnode_add_reference(filp->u.vnode);

  Info("vnode_get_from_filp(filp:%08x) vnode:%08x", (uint32_t)filp, (uint32_t)filp->u.vnode);
  
  return filp->u.vnode;
}

