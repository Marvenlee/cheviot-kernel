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
 * Manages a process's file descriptor tables, current directory and current root. 
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>


/* @brief   Allocate and initialize a process's filesystem state
 *
 * @param   proc, process whose filesystem state shall be initialized
 * @return  0 on success, negative errno on error
 */
int init_fproc(struct Process *proc)
{
  struct FProcess *fproc;
  
  fproc = kmalloc_page();

  if (fproc == NULL) {
    return -ENOMEM;
  }
    
  proc->fproc = fproc;

  FD_ZERO(&fproc->fd_in_use_set);
  FD_ZERO(&fproc->fd_close_on_exec_set);

  for (int t=0; t<OPEN_MAX; t++) {  
    fproc->fd_table[t] = NULL;
  }  

  return 0;
}


/* @brief   Close open files of process and free filesystem resources
 *
 * @param   proc, process whose files are to be closed
 * @return  0 on success, negative errno on error
 */
int fini_fproc(struct Process *proc)
{  
  struct FProcess *fproc = proc->fproc;
  
  KASSERT(fproc != NULL);
  
  for (int fd = 0; fd < OPEN_MAX; fd++) {
      do_close(proc, fd);
  }
  
  if (fproc->current_dir != NULL) {
    vnode_put(fproc->current_dir);
  }

  if (fproc->root_dir != NULL) {
    vnode_put(fproc->root_dir);
  }

  kfree_page(proc->fproc);
  proc->fproc = NULL;
  return 0;
}


/* @brief   Fork the filesystem state of one process into another
 * 
 */
int fork_process_fds(struct Process *newp, struct Process *oldp)
{
  struct Filp *filp;
  struct FProcess *new_fproc;
  struct FProcess *old_fproc;
  
  new_fproc = kmalloc_page();

  if (new_fproc == NULL) {
    return -ENOMEM;
  }
    
  old_fproc = oldp->fproc;
  newp->fproc = new_fproc;
  
  new_fproc->current_dir = old_fproc->current_dir;
    
  if (new_fproc->current_dir != NULL) {
    vnode_add_reference(new_fproc->current_dir);
  }

  new_fproc->root_dir = old_fproc->root_dir;

  if (new_fproc->root_dir != NULL) {
    vnode_add_reference(new_fproc->root_dir);
  }

  for (int fd = 0; fd < OPEN_MAX; fd++) {          
    if (old_fproc->fd_table[fd] != NULL) {
      filp = old_fproc->fd_table[fd];
      new_fproc->fd_table[fd] = filp;
      filp->reference_cnt++;
      
      FD_SET(fd, &new_fproc->fd_in_use_set);

      if (FD_ISSET(fd, &old_fproc->fd_close_on_exec_set)) {
        FD_SET(fd, &new_fproc->fd_close_on_exec_set);
      } else {
        FD_CLR(fd, &new_fproc->fd_close_on_exec_set);
      }
    } else {
      new_fproc->fd_table[fd] = NULL;
      FD_CLR(fd, &new_fproc->fd_in_use_set);
      FD_CLR(fd, &new_fproc->fd_close_on_exec_set);        
    }
  }

  return 0;
}



