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
  Info("init_fproc(proc:%08x)", (uint32_t)proc);
  Info("open_max = %d", OPEN_MAX);

/*  
  fproc = kmalloc_page();

  if (fproc == NULL) {
    Info("init_fproc -enomem");
    return -ENOMEM;
  }
    
  proc->fproc = fproc;
*/

  proc->fproc.fd_table = kmalloc_page();

  if (proc->fproc.fd_table == NULL) {
    Info("init_fproc -enomem");
    return -ENOMEM;
  }

  proc->fproc.umask = 0;
  proc->fproc.current_dir = NULL;
  proc->fproc.root_dir = NULL;

  for (int t=0; t<OPEN_MAX; t++) {  
    proc->fproc.fd_table[t].filp = NULL;
    proc->fproc.fd_table[t].flags = FDF_NONE;    
  }

  Info("fproc fd table initialized");

  return 0;
}


/* @brief   Close open files of process and free filesystem resources
 *
 * @param   proc, process whose files are to be closed
 * @return  0 on success, negative errno on error
 *
 * Note that no file descriptors should be busy.
 */
int fini_fproc(struct Process *proc)
{  
  for (int fd = 0; fd < OPEN_MAX; fd++) {
      do_close(proc, fd);
  }
  
  if (proc->fproc.current_dir != NULL) {
    vnode_put(proc->fproc.current_dir);
  }

  if (proc->fproc.root_dir != NULL) {
    vnode_put(proc->fproc.root_dir);
  }

  proc->fproc.current_dir = NULL;
  proc->fproc.root_dir = NULL;

  kfree_page(proc->fproc.fd_table);
  return 0;
}






