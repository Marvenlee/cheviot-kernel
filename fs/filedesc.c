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
  
  Info("sys_fcntl(fd:%d, cmd:%d, arg:%08x", fd, cmd, arg);
  
  current = get_current_process();

  if ((current->fproc.fd_table[fd].flags & FDF_VALID) == 0) {  
    Info("fd %d not valid -EBADF", fd);
    return -EBADF;
  }
    
  if ((filp = filp_get(current, fd)) == NULL) {
    Info("fd %d cannot get filp -EINVAL", fd);
    return -EINVAL;
  }
    
	switch(cmd) {
    case F_DUPFD:	/* Duplicate fildes */
	    if (arg < 0 || arg >= OPEN_MAX) {
        Info("Fcntl F_DUPFD -EBADF");
        return -EBADF;
      }

      new_fd = dup_fd(current, fd, arg, OPEN_MAX);	      

      Info("dup_fd old fd:%d, new fd:%d", fd, new_fd);
      return new_fd;
  	
    case F_GETFD:	/* Get fildes flags */      
      if (current->fproc.fd_table[fd].flags & FDF_CLOSE_ON_EXEC) {
        return 1; 
      } else {  
        return 0;
      }
        		  
		case F_SETFD:	/* Set fildes flags */
      if (arg) {
        current->fproc.fd_table[fd].flags |= FDF_CLOSE_ON_EXEC;
      } else {
        current->fproc.fd_table[fd].flags &= ~FDF_CLOSE_ON_EXEC;
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

  if (current->fproc.fd_table[new_fd].flags == FDF_VALID) {
    KASSERT(current->fproc.fd_table[new_fd].filp != NULL);
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
  struct FileDesc *filedesc;
  int new_fd;
  
  Info("dup_fd(fd:%d, min:%d, max:%d)", fd, min_fd, max_fd);
  
  filp = filp_get(proc, fd);
  
  if (filp == NULL) {
    return -EINVAL;
  }
  
  new_fd = fd_alloc(proc, min_fd, max_fd, &filedesc);
  
  if (new_fd < 0) {
    return -EMFILE;
  }
  
  filedesc->filp = filp;
  filp->reference_cnt++;

  filedesc->flags = FDF_VALID;    // FIXME: Do we copy CLOSE_ON_EXEC ?

  return new_fd;
}


/* @brief   Mark entry in file descriptor table as in use
 *
 */
int fd_alloc(struct Process *proc, int min_fd, int max_fd, struct FileDesc **filedesc)
{  
  Info("fd_alloc(proc:%08x, min:%d, max:%d, fdesc**:%08x)", (uint32_t)proc, min_fd, max_fd, (uint32_t)filedesc);

  for (int fd=min_fd; fd <= max_fd; fd++) {
    if ((proc->fproc.fd_table[fd].flags & FDF_VALID) == 0) {
      proc->fproc.fd_table[fd].filp = NULL;
      proc->fproc.fd_table[fd].flags = FDF_VALID;
      
      *filedesc = &proc->fproc.fd_table[fd];      
      return fd; 
    }
  }
  
  Error("fd_alloc failed");
  
  *filedesc = NULL;
  return -EMFILE;
}


/* @brief   Mark entry in file descriptor table as free
 * 
 */
int fd_free(struct Process *proc, int fd)
{
  Info("fd_free(proc:%08x, fd:%d)", (uint32_t)proc, fd);
  
  if (fd < 0 || fd >= OPEN_MAX) {
    Error("fd out of range");
    return -EINVAL;
  }

  Info("proc->fproc.fd_table = %08x", (uint32_t)proc->fproc.fd_table);
  Info("&proc->fproc.fd_table[fd] = %08x", (uint32_t)&proc->fproc.fd_table[fd]); 
    
  if ((proc->fproc.fd_table[fd].flags & FDF_VALID) == 0 && proc->fproc.fd_table[fd].filp == NULL) {
    return -EINVAL;  
  }

  proc->fproc.fd_table[fd].filp = NULL;
  proc->fproc.fd_table[fd].flags = FDF_NONE;
  
  return 0;
}


/* @brief   Fork the filesystem state of one process into another
 * 
 */
int fork_fds(struct Process *newp, struct Process *oldp)
{
  int sc;
  
  Info("fork_fds(newp:%08x, oldp:%08x", (uint32_t)newp, (uint32_t)oldp);
  
  if ((sc = init_fproc(newp)) != 0) {
    Info("init_fproc failed in fork_fds, sc:%d", sc);
    return sc;
  }
  
  newp->fproc.umask = oldp->fproc.umask;  
  newp->fproc.current_dir = oldp->fproc.current_dir;
    
  if (newp->fproc.current_dir != NULL) {
    vnode_add_reference(newp->fproc.current_dir);
  }

  newp->fproc.root_dir = oldp->fproc.root_dir;

  if (newp->fproc.root_dir != NULL) {
    vnode_add_reference(newp->fproc.root_dir);
  }

  for (int fd = 0; fd < OPEN_MAX; fd++) {          
    if (oldp->fproc.fd_table[fd].flags & FDF_VALID) {
      KASSERT(oldp->fproc.fd_table[fd].filp != NULL);
      
      newp->fproc.fd_table[fd].filp = oldp->fproc.fd_table[fd].filp;      
      newp->fproc.fd_table[fd].flags = oldp->fproc.fd_table[fd].flags;
      newp->fproc.fd_table[fd].filp->reference_cnt++;            
    } else {
      newp->fproc.fd_table[fd].filp = NULL;
      newp->fproc.fd_table[fd].flags = FDF_NONE;
    }
  }

  return 0;
}


/* @brief   Close file descriptors marked as "close on exec" during an exec syscall
 *
 * @return  0 on sucess, negative errno on failure
 *
 * Close any file descriptors marked as "close on exec" and reset the close on exec
 * flag.
 */
int exec_fds(struct Process *proc)
{
  for (int fd = 0; fd < OPEN_MAX; fd++) {
    if (proc->fproc.fd_table[fd].flags & FDF_CLOSE_ON_EXEC) {
      proc->fproc.fd_table[fd].flags &= ~FDF_CLOSE_ON_EXEC;
      do_close(proc, fd);      
    }
  }

  return 0;
}


