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
#include <kernel/proc.h>
#include <kernel/types.h>
#include <sys/mount.h>


/* @brief   Get the file statistics of a named file
 *
 * @param   _path, pathname to file to gather statistics of
 * @param   _stat, pointer to stat structure to return statistics
 * @return  0 on success, negative errno on error
 */
int sys_stat(char *_path, struct stat *_stat) {
  struct stat stat;
  struct lookupdata ld;
  int sc;

  if ((sc = lookup(_path, 0, &ld)) == 0) {
    rwlock(&ld.vnode->lock, LK_SHARED);
    
    stat.st_dev = ld.vnode->superblock->dev;
    stat.st_ino = ld.vnode->inode_nr;
    stat.st_mode = ld.vnode->mode;
    stat.st_nlink = ld.vnode->nlink;
    stat.st_uid = ld.vnode->uid;
    stat.st_gid = ld.vnode->gid;
    stat.st_rdev = ld.vnode->rdev;
    stat.st_size = ld.vnode->size;
    stat.st_atime = ld.vnode->atime;
    stat.st_mtime = ld.vnode->mtime;
    stat.st_ctime = ld.vnode->ctime;

	  // TODO: Handle st_blocks and st_blocksize correctly 
	  // Update these on reads, writes or truncate operations.
	  // special case return zeros for char and other non-block devices?


	  if (ld.vnode->superblock->block_size != 0) {
		  stat.st_blocks = ld.vnode->size / ld.vnode->superblock->block_size;
		  stat.st_blksize = ld.vnode->superblock->block_size;
	  } else {
		  stat.st_blocks = 0;
		  stat.st_blksize = 0;
	  }

    rwlock(&ld.vnode->lock, LK_RELEASE);

    lookup_cleanup(&ld);
    sc = CopyOut(_stat, &stat, sizeof stat);    
  }
  
  return sc;
}


/* @brief   Get the file statistics of an open file
 *
 * @param   fd, file handle of file to gather statistics of
 * @param   _stat, pointer to stat structure to return statistics
 * @return  0 on success, negative errno on error 
 */
int sys_fstat(int fd, struct stat *_stat)
{
  struct Filp *filp;
  struct VNode *vnode;
  struct stat stat;
  struct Process *current;
  int sc;

  current = get_current_process();

  filp = filp_get(current, fd);
  
  if (filp) {
    vnode = vnode_get_from_filp(filp);

    if (vnode) {
      rwlock(&vnode->lock, LK_SHARED);
      
      stat.st_dev = vnode->superblock->dev;
      stat.st_ino = vnode->inode_nr;
      stat.st_mode = vnode->mode;  
      stat.st_nlink = vnode->nlink;
      stat.st_uid = vnode->uid;
      stat.st_gid = vnode->gid;
      stat.st_rdev = vnode->rdev;
      stat.st_size = vnode->size;
      stat.st_atime = vnode->atime;
      stat.st_mtime = vnode->mtime;
      stat.st_ctime = vnode->ctime;

	    // TODO: Handle st_blocks and st_blocksize correctly 
	    // Update these on reads, writes or truncate operations.
	    // special case return zeros for char and other non-block devices?

	    if (vnode->superblock->block_size != 0) {
		    stat.st_blocks = vnode->size / vnode->superblock->block_size;
		    stat.st_blksize = vnode->superblock->block_size;
	    } else {
		    stat.st_blocks = 0;
		    stat.st_blksize = 0;
	    }

      rwlock(&vnode->lock, LK_RELEASE);

      vnode_put(vnode);

      sc = CopyOut(_stat, &stat, sizeof stat);
    } else {
      sc = -EINVAL;
    }    

    filp_put(filp);
  } else {
    sc = -EBADF;
  }
    
  return sc;
}

