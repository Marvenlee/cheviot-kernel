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
      klog_info("-----------------------------------------------");
      klog_info("pid: %d, name: %s", proc->pid, proc->basename);
      klog_info("sid: %d, pgid: %d", proc->pid, proc->pgid);
      klog_info("flags:%08x, state:%d", proc->flags, proc->state);
      klog_info("uid:%d, gid:%d, euid:%d, egid:%d", proc->uid, proc->gid, proc->euid, proc->egid);
      klog_info("suid:%d, sgid:%d", proc->suid, proc->sgid);
      klog_info("ngroups:%d", proc->ngroups);
      klog_info("parent:%08x", (uint32_t)proc->parent);
      klog_info("log_level:%d",proc->log_level);
      klog_info("exit_status:%08x, exit_in_progress:%d", proc->exit_status, proc->exit_in_progress);
      klog_info("privileges:%08x,%08x", (uint32_t)(proc->privileges >> 32), (uint32_t)proc->privileges);
      klog_info("privileges_after_exec:%08x,%08x", (uint32_t)(proc->privileges_after_exec >> 32), (uint32_t)proc->privileges_after_exec);        

      for (int s=0; s<FILEDESC_MAX; s++) {
        if ((proc->fproc.fd_table[s].flags & (FDF_VALID | FDF_ALLOCED)) != 0) {
          klog_info("-> fd:%d, filp:%08x, flags:%08x", s, proc->fproc.fd_table[s].filp, proc->fproc.fd_table[s].flags);    
        }
      }
    }
  }

  klog_info("-----------------------------------------------");
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
      klog_info("-----------------------------------------------");
      klog_info("filp:%08x, type:%d", (uint32_t)filp, filp->type);
      klog_info("reference_cnt:%d", filp->reference_cnt);
      klog_info("object: %08x", (uint32_t)filp->u.vnode);
      klog_info("offset: %08x, mode:%o", (uint32_t)filp->offset, filp->mode);
      klog_info("flags: %08x", filp->flags);
    }
  }
  
  klog_info("-----------------------------------------------");  
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
      klog_info("-----------------------------------------------");
      klog_info("vnode:%08x", (uint32_t)vnode);
      klog_info("inode_nr:%d, superblock:%08x", vnode->inode_nr, (uint32_t)vnode->superblock);
      klog_info("reference_cnt:%d", vnode->reference_cnt);
      klog_info("flags:%08x", vnode->flags);
      klog_info("char_read_busy:%d, char_write_busy:%d", vnode->char_read_busy, vnode->char_write_busy);
      klog_info("vnode_mounted_here:%08x, vnode_covered:%08x", (uint32_t)vnode->vnode_mounted_here, (uint32_t)vnode->vnode_covered);
      klog_info("pipe:%08x", (uint32_t)vnode->pipe);
      klog_info("tty_sid:%d", vnode->tty_sid);
      klog_info("mode:%0o oct", vnode->mode);
      
      if (S_ISCHR(vnode->mode)) {
        klog_info("mode is ISCHR");
      } else if (S_ISREG(vnode->mode)) {
        klog_info("mode is ISREG");
      } else if (S_ISDIR(vnode->mode)) {
        klog_info("mode is ISDIR");
      } else if (S_ISFIFO(vnode->mode)) {
        klog_info("mode is ISFIFO");
      } else if (S_ISBLK(vnode->mode)) {
        klog_info("mode is ISBLK");
      } else if (S_ISSOCK(vnode->mode)) {
        klog_info("mode is ISSOCK");
      } else {
        klog_info("mode file type unknown");
      }

      if (vnode == root_vnode) {
        klog_info("vnode is root /");
      }
      
      klog_info("uid:%d, gid:%d", vnode->uid, vnode->gid);
      klog_info("size:%u", (uint32_t)vnode->size);

      if (S_ISFIFO(vnode->mode)) {
        klog_info("-> pipe reader: %d, writer: %d", vnode->pipe->reader_cnt, vnode->pipe->writer_cnt);
      }
    }
  }

  klog_info("-----------------------------------------------");
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
      klog_info("-----------------------------------------------");
      klog_info("sb:%08x", (uint32_t)sb);
      klog_info("reference_cnt:%d", sb->reference_cnt);
      klog_info("dev:%08x, flags:%08x", (uint32_t)sb->dev, (uint32_t)sb->flags);
      klog_info("size:%u, block_size:%u", (uint32_t)sb->size, (uint32_t)sb->block_size);
      klog_info("root:%08x, vnode_list_busy:%d", (uint32_t)sb->root, sb->vnode_list_busy);
    }
  }

  klog_info("-----------------------------------------------");

}


/*
 *
 */
void dump_kernel_pipes(int cmd, int arg1, int arg2)
{
}


