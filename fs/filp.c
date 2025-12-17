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

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>

KLOG_REGISTER(LOG_FS_FILP)


/* @brief   Lookup a file pointer from a file descriptor
 *
 * @param   proc, process that the file descriptor belongs to
 * @param   fd, file descriptor to look up
 * @return  filp (file pointer) structure
 */
struct Filp *filp_get(struct Process *proc, int fd)
{
  struct Filp *filp;
  
  klog_info("filp_get(proc:%08x, fd:%d)", (uint32_t)proc, fd);
  
  if (fd < 0 || fd >= FILEDESC_MAX) {
    klog_info("filp_get, fd:%d error out of range", fd);
    return NULL;
  }

  if ((proc->fproc.fd_table[fd].flags & FDF_VALID) == 0) {
    klog_info("get_filp() fd:%d, error not valid", fd);
    return NULL;
  }
  
  if (proc->fproc.fd_table[fd].filp == NULL) {
    klog_error("get_filp() fd:%d, error no filp", fd);
    return NULL;
  }
  
  filp = proc->fproc.fd_table[fd].filp;
  
  // TODO: filp->reference_cnt++;
  
  klog_info(".. filp_get(fd:%d) => filp: %08x, ref_cnt:%d", fd, (uint32_t)filp, filp->reference_cnt);
  
  return filp;
}




/* @brief   Allocate a filp (file pointer)
 */
struct Filp *filp_get_new(void)
{
  struct Filp *filp;

  klog_info("filp_get_new()");

  filp = LIST_HEAD(&filp_free_list);

  if (filp == NULL) {
    klog_info("filp_alloc() failed, out of filps");
    return NULL;
  }

  kassert(filp->type == FILP_TYPE_FREE);

  LIST_REM_HEAD(&filp_free_list, filp_entry);
  filp->reference_cnt = 1;
  filp->type = FILP_TYPE_UNDEF;
  memset(&filp->u, 0, sizeof filp->u);
  
  filp->flags = 0;

  klog_info("filp_get_new() filp:%08x, ref_cnt = %d", (uint32_t)filp, filp->reference_cnt);  
  
  return filp;
}


/*
 *
 */
void filp_ref(struct Filp *filp)
{
  kassert(filp != NULL);
  
  filp->reference_cnt++;

  klog_info("filp_ref(%08x) ref_cnt now:%d", (uint32_t)filp, filp->reference_cnt);
}


/*
 *
 */
int filp_release(struct Filp *filp)
{
  int retval;
  
  kassert (filp != NULL);

  filp->reference_cnt--;

  klog_info("filp_release(%08x) ref_cnt now:%d", (uint32_t)filp, filp->reference_cnt);

  retval = filp->reference_cnt;

  if (filp->reference_cnt == 0) {
    klog_info("filp:%08x ref_cnt is 0, setting type to FILP_TYPE_FREE");
    filp->type = FILP_TYPE_FREE;

    klog_info("setting filp->u to NULL, adding to free list");
    memset(&filp->u, 0, sizeof filp->u);
    LIST_ADD_HEAD(&filp_free_list, filp, filp_entry);
  }
  
  return retval;
}


