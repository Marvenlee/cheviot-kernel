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
#include <kernel/vm.h>
#include <kernel/utility.h>
#include <poll.h>
#include <string.h>
#include <sys/mount.h>


/*
 * Get the superblock of a file descriptor created by sys_mount()
 */
struct SuperBlock *get_superblock(struct Process *proc, int fd)
{
  struct Filp *filp;

  KASSERT(proc != NULL);
  
  filp = filp_get(proc, fd);
    
  if (filp == NULL) {
    Error("get_superblock, filp is NULL");
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_SUPERBLOCK) {
    Error("get_superblock, filp type is not SUPERBLOCK");
    return NULL;
  }
    
  return filp->u.superblock;
}


/* @brief   Allocate a superblock structure
 *
 * Preconditions: The superblock list lock is exclusive locked or draining
 */
struct SuperBlock *alloc_superblock(void)
{
  struct SuperBlock *sb;

  Info("alloc_superblock()");
  
  KASSERT(superblock_list_lock.exclusive_cnt == 1 || superblock_list_lock.is_draining == true);
  
  sb = LIST_HEAD(&free_superblock_list);

  if (sb == NULL) {
    Error("no free superblocks");
    rwlock(&superblock_list_lock, LK_RELEASE);
    return NULL;
  }

  LIST_REM_HEAD(&free_superblock_list, link);

//  memset(sb, 0, sizeof *sb);
 
  // FIXME superblock reference_cnt
  sb->reference_cnt = 0;
  sb->dev = 0xdead;
  sb->flags = 0;
  
  LIST_INIT(&sb->vnode_list);
  
  // TODO: Will need to hold mounted_superblock_list_busy rwlock EXCLUSIVE
  // Unless we can make temporary copies of lists at a given instant and make
  // sure each superblock has been referenced.
  
  LIST_ADD_TAIL(&mounted_superblock_list, sb, link);

  Info("rwlock superblock_list_lock RELEASE");

  return sb;
}


/* @brief   Free a superblock structure
 *
 * Preconditions: The superblock list lock is exclusive locked or draining
 *
 */
void free_superblock(struct SuperBlock *sb)
{
  KASSERT (sb != NULL);

  Info("free_superblock()");

  KASSERT(superblock_list_lock.exclusive_cnt == 1 || superblock_list_lock.is_draining == true);
  KASSERT(sb->reference_cnt == 0);
  
  LIST_REM_ENTRY(&mounted_superblock_list, sb, link);  
  LIST_ADD_TAIL(&free_superblock_list, sb, link);
}


/*
 *
 */
void discard_vnodes(struct SuperBlock *sb)
{
  struct VNode *vnode;
  
  while ((vnode = LIST_HEAD(&sb->vnode_list)) != NULL) {
    LIST_REM_HEAD(&sb->vnode_list, vnode_link);             // TODO: Move into vnode_discard?    
    
    vnode_discard(vnode);     // Performs knote removal, cache block removal.
  } 
}


/*
 *
 */

void ref_superblock(struct SuperBlock *sb)
{
  sb->reference_cnt++;
}


/* @brief   Common code for closing message port from client or server side.
 *
 * Note: There is 1 reference count due to the mount point.
 *
 * TODO: Client: when closing vnodes, need to check if we are the last one.
 * Need to check or grab the sync lock
 * Free the superblock/message port.  
 * Free the vnode and filp/fd
 *
 * May need to temporarily "up" the reference cnt, in order to avoid another thread
 * closing it at the same time.
 *
 * FIXME: Is this recursive?  Is this called when we discard vnodes?
 * FIXME:  How can this superblock be dereferenced if there are cached/unused vnodes?
 * Won't they have incremented the sb->reference_cnt?
 */
int deref_superblock(struct SuperBlock *sb)
{
  sb->reference_cnt--;
    
  if (sb->reference_cnt == 0) {
    // discard_vnodes(sb);   
    free_superblock(sb);    
  }
  
  return 0;
}


/* @brief   Unblock threads waiting on character device reader and writer queues
 *
 * TODO: Unblock threads waiting on character device locks that allow
 * only a single reader and single writer and single I/O message simultaneously.
 *
 */
void fini_character_device_queues(struct SuperBlock *sb)
{
  struct VNode *vnode;
  
  vnode = LIST_HEAD(&sb->vnode_list);
  
  while (vnode != NULL) {
    vnode->char_read_busy = false;
    vnode->char_write_busy = false;

    TaskWakeupAll(&vnode->rendez);

    vnode = LIST_NEXT(vnode, vnode_link);   
  }
}


/* @brief   Do the unmounting of the superblock root vnode from the covered vnode.
 *
 * This detaches the mount point and decrements the reference counts of the
 * covered and mounted root vnode.
 *
 * If the vnodes then get zero reference counts, do we need to free them here.
 * Same for superblocks.
 *
 * Does this cleanup get recursive which we don't want?
 */
void detach_mount(struct SuperBlock *sb)
{
  struct VNode *mount_root_vnode;
  struct VNode *covered_vnode;
  
  mount_root_vnode = sb->root;        
  covered_vnode = mount_root_vnode->vnode_covered;
  
  covered_vnode->vnode_mounted_here = NULL;
  mount_root_vnode->vnode_covered = NULL;
  
  vnode_put(covered_vnode);
  vnode_put(mount_root_vnode);
    
  sb->root = NULL;
  
  TaskWakeup(&sb->rendez);
}


/* @brief   Close a file descriptor pointing to a superblock/msgport
 *
 *
 *
 * TODO: Need to have separate function to sync the device,
 * flush all pending delayed writes and anything in message queue
 * Prevent further access
 * Perhaps separate umount(); ???
 */ 
int close_superblock(struct SuperBlock *sb)
{
  struct Filp *filp;
  int sc;
  
  Info("close_superblock()");
  
//  KASSERT(sb != NULL);

  sb->flags |= SBF_ABORT;
  
  detach_mount(sb);
  fini_msgport(&sb->msgport);
  fini_character_device_queues(sb); 
      
  sc = deref_superblock(sb);

  return sc;  
}



