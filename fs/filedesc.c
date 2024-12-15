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


/* @brief   fcntl system call
 */
int sys_fcntl(int fd, int cmd, int arg)
{
  struct Process *current;
  struct Filp *filp;
	int new_fd;
    
  current = get_current_process();
  
  if ((filp = get_filp(current, fd)) == NULL) {
    Info("Fcntl fd %d does not exist", fd);
    return -EINVAL;
  }
  
  if (!FD_ISSET(fd, &current->fproc->fd_in_use_set)) {  
    return -EBADF;
  }
  
	switch(cmd) {
    case F_DUPFD:	/* Duplicate fildes */
	    if (arg < 0 || arg >= OPEN_MAX) {
        Info("Fcntl F_DUPFD -EBADF");
        return -EBADF;
      }

      new_fd = dup_fd(current, fd, arg, OPEN_MAX);	      
      return new_fd;
  	
    case F_GETFD:	/* Get fildes flags */
      
      if (FD_ISSET(fd, &current->fproc->fd_close_on_exec_set)) {
        return 1; 
      } else {  
        return 0;
      }
        		  
		case F_SETFD:	/* Set fildes flags */
	    if (FD_ISSET(fd, &current->fproc->fd_in_use_set)) {
	      if (arg) {
          FD_SET(fd, &current->fproc->fd_close_on_exec_set);
	      } else {
          FD_CLR(fd, &current->fproc->fd_close_on_exec_set);
        }
      }
			return arg;
      		
		case F_GETFL:	/* Get file flags */
      Info("Fcntl F_GETFL unimplemented");

      // TODO: Effectively open flags bit O_RW, O_APPEND, O_NONBLOCK
      //  TODO: Add as oflags state to filp on open() or any vnode creation
			return -EINVAL;
      
		case F_SETFL:	/* Set file flags */
      Info("Fcntl F_SETFL unimplemented");

      // TODO: Effectively open flags bit O_RW, O_APPEND, O_NONBLOCK
			return -EINVAL;
      
		default:
		  Error("Fcntl ENOSYS");
			return -ENOSYS;
	}

  Error("Fcntl -EINVAL");
	return -EINVAL;
}


/* @brief   dup system call
 */
int sys_dup(int fd)
{
  int new_fd;
  struct Process *current;
  
  current = get_current_process();

  new_fd = dup_fd(current, fd, 0, OPEN_MAX);
  return new_fd;
}


/* @brief   dup2 system call
 */
int sys_dup2(int fd, int new_fd)
{
  struct Process *current;

  Info("sys_dup2(fd:%d, new_fd:%d", fd, new_fd);

  current = get_current_process();

  if (fd < 0 || fd >= OPEN_MAX || new_fd < 0 || new_fd >= OPEN_MAX) {
    return -EINVAL;
  }

  if (current->fproc->fd_table[new_fd] != NULL) {
    do_close(current, new_fd);    
  }
    
  new_fd = dup_fd(current, fd, new_fd, new_fd);

  Info("res:%d of sys_dup2", new_fd);

  return new_fd;
}


/*
 *
 */
int dup_fd(struct Process *proc, int fd, int min_fd, int max_fd)
{
  struct Filp *filp;
  int new_fd;
  
  filp = get_filp(proc, fd);
  
  if (filp == NULL) {
    return -EINVAL;
  }
  
  new_fd = alloc_fd(proc, min_fd, max_fd);
  
  if (new_fd < 0) {
    return -EMFILE;
  }
  
  proc->fproc->fd_table[new_fd] = filp;
  filp->reference_cnt++;
  return new_fd;
}


/* @brief   Mark entry in file descriptor table as in use
 *
 */
int alloc_fd(struct Process *proc, int min_fd, int max_fd)
{  
  for (int fd=min_fd; fd <= max_fd; fd++) {
    if (FD_ISSET(fd, &proc->fproc->fd_in_use_set) == 0) {
      proc->fproc->fd_table[fd] = NULL;
      FD_SET(fd, &proc->fproc->fd_in_use_set);
      FD_CLR(fd, &proc->fproc->fd_close_on_exec_set);
      return fd; 
    }
  }
  
  Error("alloc_fd failed");
  return -EMFILE;
}


/* @brief   Mark entry in file descriptor table as free
 * 
 */
int free_fd(struct Process *proc, int fd)
{
  if (fd < 0 || fd >= OPEN_MAX) {
    return -EINVAL;
  }
    
  if (proc->fproc->fd_table[fd] == NULL) {
    return -EINVAL;  
  }

  proc->fproc->fd_table[fd] = NULL;
  FD_CLR(fd, &proc->fproc->fd_in_use_set);
  FD_CLR(fd, &proc->fproc->fd_close_on_exec_set);
    
  return 0;
}


/* @brief   Enable and configure a filp
 *
 */
int set_fd(struct Process *proc, int fd, int type, uint32_t flags, void *item)
{
  struct Filp *filp;
  
  filp = get_filp(proc, fd);
  
  if (filp == NULL) {
    Error("set_fd, filp invalid");
    return -EINVAL;  
  }

  filp->type = type;
  
  switch (type)
  {
    case FILP_TYPE_VNODE:
      Info("Filp type vnode");
      filp->u.vnode = item;
      break;
    case FILP_TYPE_SUPERBLOCK:
      Info("Filp type superblock");
      filp->u.superblock = item;
      break;
    case FILP_TYPE_KQUEUE:
      Info("Filp type kqueue");
      filp->u.kqueue = item;
      break;
  }

  FD_SET(fd, &proc->fproc->fd_in_use_set);
  
  if (flags & FD_FLAG_CLOEXEC) {
    FD_SET(fd, &proc->fproc->fd_close_on_exec_set);
  }
  
  return 0;
}


/* @brief   Close file descriptors during an exec syscall
 *
 * @return  0 on sucess, negative errno on failure
 */
int close_on_exec_process_fds(void)
{
  struct Process *current;
  
  current = get_current_process();

  for (int fd = 0; fd < OPEN_MAX; fd++) {
    if (FD_ISSET(fd, &current->fproc->fd_close_on_exec_set)) {
      do_close(current, fd);
    }
  }

  return 0;
}




