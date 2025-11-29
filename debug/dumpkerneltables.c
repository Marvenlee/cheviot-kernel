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

#define KDEBUG

#include <sys/debug.h>
#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>


/* @brief   Dump kernel data structures to console
 *
 */
int sys_dumpkerneltables(int cmd, uint32_t arg1, uint32_t arg2)
{
  switch(cmd) {
    case KDUMP_KERNEL_PROCESSES:
      dump_kernel_processes(cmd, arg1, arg2);
      break;

    case KDUMP_KERNEL_FILPS:
      dump_kernel_filps(cmd, arg1, arg2);
      break;

    case KDUMP_KERNEL_VNODES:
      dump_kernel_vnodes(cmd, arg1, arg2);
      break;

    case KDUMP_KERNEL_SUPERBLOCKS:
      dump_kernel_superblocks(cmd, arg1, arg2);
      break;

    case KDUMP_KERNEL_PIPES:
      dump_kernel_pipes(cmd, arg1, arg2);
      break;

    default:      
      break;
  }

  return 0;
}


/*
 * Need to check if any struct is in use or on free list
 */
void dump_kernel_processes(int cmd, int arg1, int arg2)
{
  struct Process *proc;
  uint32_t base, nprocess;
  
  base = arg1;
  nprocess = max_process - base;
  
  if (arg2 < nprocess) {
    nprocess = arg2;
  }
      
  for(uint32_t t=base; t < base + nprocess; t++) {
    proc = &process_table[t];
    
    if (proc->state != PROC_STATE_FREE) {
      Info("-----------------------------------------------");
      Info("pid: %d, name: %s", proc->pid, proc->basename);
      Info("sid: %d, pgid: %d", proc->pid, proc->pgid);
      Info("flags:%08x, state:%d", proc->flags, proc->state);
      Info("uid:%d, gid:%d, euid:%d, egid:%d", proc->uid, proc->gid, proc->euid, proc->egid);
      Info("suid:%d, sgid:%d", proc->suid, proc->sgid);
      Info("ngroups:%d", proc->ngroups);
      Info("parent:%08x", (uint32_t)proc->parent);
      Info("log_level:%d",proc->log_level);
      Info("exit_status:%08x, exit_in_progress:%d", proc->exit_status, proc->exit_in_progress);
      Info("privileges:%08x,%08x", (uint32_t)(proc->privileges >> 32), (uint32_t)proc->privileges);
      Info("privileges_after_exec:%08x,%08x", (uint32_t)(proc->privileges_after_exec >> 32), (uint32_t)proc->privileges_after_exec);        

      for (int s=0; s<FILEDESC_MAX; s++) {
        if ((proc->fproc.fd_table[s].flags & (FDF_VALID | FDF_ALLOCED)) != 0) {
          Info("-> fd:%d, filp:%08x, flags:%08x", s, proc->fproc.fd_table[s].filp, proc->fproc.fd_table[s].flags);    
        }
      }
    }
  }

  Info("-----------------------------------------------");
}


/*
 *
 */
void dump_kernel_filps(int cmd, int arg1, int arg2)
{
  struct Filp *filp;
  uint32_t base, nfilp;
  
  base = arg1;
  nfilp = max_filp - base;
  
  if (arg2 < nfilp) {
    nfilp = arg2;
  }
  
  for(uint32_t t=base; t < base + nfilp; t++) {
    filp = &filp_table[t];

    if (filp->type != FILP_TYPE_UNDEF) {
      Info("-----------------------------------------------");
      Info("filp:%08x, type:%d", (uint32_t)filp, filp->type);
      Info("reference_cnt:%d", filp->reference_cnt);
      Info("object: %08x", (uint32_t)filp->u.vnode);
      Info("offset: %08x, mode:%o", (uint32_t)filp->offset, filp->mode);
      Info("flags: %08x", filp->flags);
    }
  }
  
  Info("-----------------------------------------------");  
}


/*
 *
 */
void dump_kernel_vnodes(int cmd, int arg1, int arg2)
{
  struct VNode *vnode;
  uint32_t base, nvnode;
  
  base = arg1;
  nvnode = max_vnode - base;
  
  if (arg2 < nvnode) {
    nvnode = arg2;
  }
      
  for(uint32_t t=base; t < base + nvnode; t++) {
    vnode = &vnode_table[t];

    if (vnode->flags & V_VALID) {
      Info("-----------------------------------------------");
      Info("vnode:%08x", (uint32_t)vnode);
      Info("inode_nr:%d, superblock:%08x", vnode->inode_nr, (uint32_t)vnode->superblock);
      Info("reference_cnt:%d", vnode->reference_cnt);
      Info("flags:%08x", vnode->flags);
      Info("char_read_busy:%d, char_write_busy:%d", vnode->char_read_busy, vnode->char_write_busy);
      Info("vnode_mounted_here:%08x, vnode_covered:%08x", (uint32_t)vnode->vnode_mounted_here, (uint32_t)vnode->vnode_covered);
      Info("pipe:%08x", (uint32_t)vnode->pipe);
      Info("tty_sid:%d", vnode->tty_sid);
      Info("mode:%0o oct", vnode->mode);
      
      if (S_ISCHR(vnode->mode)) {
        Info("mode is ISCHR");
      } else if (S_ISREG(vnode->mode)) {
        Info("mode is ISREG");
      } else if (S_ISDIR(vnode->mode)) {
        Info("mode is ISDIR");
      } else if (S_ISFIFO(vnode->mode)) {
        Info("mode is ISFIFO");
      } else if (S_ISBLK(vnode->mode)) {
        Info("mode is ISBLK");
      } else if (S_ISSOCK(vnode->mode)) {
        Info("mode is ISSOCK");
      } else {
        Info("mode file type unknown");
      }

      if (vnode == root_vnode) {
        Info("vnode is root /");
      }
      
      Info("uid:%d, gid:%d", vnode->uid, vnode->gid);
      Info("size:%u", (uint32_t)vnode->size);

      if (S_ISFIFO(vnode->mode)) {
        Info("-> pipe reader: %d, writer: %d", vnode->pipe->reader_cnt, vnode->pipe->writer_cnt);
      }
    }
  }

  Info("-----------------------------------------------");
}


/*
 *
 */
void dump_kernel_superblocks(int cmd, int arg1, int arg2)
{
  struct SuperBlock *sb;
  uint32_t base, nsuperblock;
  
  base = arg1;
  nsuperblock = max_superblock - base;
  
  if (arg2 < nsuperblock) {
    nsuperblock = arg2;
  }
      
  for(uint32_t t=base; t < base + nsuperblock; t++) {
    sb = &superblock_table[t];

    if (sb->reference_cnt > 0) {
      Info("-----------------------------------------------");
      Info("sb:%08x", (uint32_t)sb);
      Info("reference_cnt:%d", sb->reference_cnt);
      Info("dev:%08x, flags:%08x", (uint32_t)sb->dev, (uint32_t)sb->flags);
      Info("size:%u, block_size:%u", (uint32_t)sb->size, (uint32_t)sb->block_size);
      Info("root:%08x, vnode_list_busy:%d", (uint32_t)sb->root, sb->vnode_list_busy);
    }
  }

  Info("-----------------------------------------------");

}


/*
 *
 */
void dump_kernel_pipes(int cmd, int arg1, int arg2)
{
}


