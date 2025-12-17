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

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <string.h>

KLOG_REGISTER(LOG_FS_FILEDESC)


/* @brief   fcntl system call
 */
int sys_fcntl(int fd, int cmd, int arg)
{
  struct Process *current;
  struct Filp *filp;
  int new_fd;

  klog_info("sys_fcntl(fd:%d, cmd:%d, arg:%d)", fd, cmd, arg);
  
  current = get_current_process();
  
  if (fd < 0 || fd >= FILEDESC_MAX) {
    klog_error("sys_fcntl() fd out of range: %d, -EINVAL", fd);
    return -EINVAL;
  }

  if ((current->fproc.fd_table[fd].flags & FDF_VALID) == 0) {  
    klog_info("fd %d not valid -EBADF", fd);
    return -EBADF;
  }
    
  if ((filp = filp_get(current, fd)) == NULL) {
    klog_info("fd %d cannot get filp -EINVAL", fd);
    return -EINVAL;
  }
    
  switch(cmd) {
    case F_DUPFD:	/* Duplicate fildes but clear close-on-exec flag */
      if (arg < 0 || arg >= FILEDESC_MAX) {
        klog_info("Fcntl F_DUPFD -EBADF");
        return -EBADF;
      }

      new_fd = dup_fd(current, fd, arg, FILEDESC_MAX, 0);	      
      return new_fd;

    case F_DUPFD_CLOEXEC:       /* Duplicate fildes and set close-on-exec flag */
      if (arg < 0 || arg >= FILEDESC_MAX) {
        klog_info("Fcntl F_DUPFD_CLOEXEC -EBADF");
        return -EBADF;
      }

      new_fd = dup_fd(current, fd, arg, FILEDESC_MAX, FDF_CLOSE_ON_EXEC);	      
      return new_fd;
  	
    case F_GETFD:	/* Get fildes flags */      
      if (current->fproc.fd_table[fd].flags & FDF_CLOSE_ON_EXEC) {
        return FD_CLOEXEC;
      } else {  
        return 0;
      }
        		  
    case F_SETFD:	/* Set fildes flags */
      if (arg & FD_CLOEXEC) {
        current->fproc.fd_table[fd].flags |= FDF_CLOSE_ON_EXEC;
      } else {
        current->fproc.fd_table[fd].flags &= ~FDF_CLOSE_ON_EXEC;
      }

      return 0;
      		
    case F_GETFL:	/* Get file flags */
      klog_info("Fcntl F_GETFL unimplemented");

      // TODO: Effectively open flags bit O_RW, O_APPEND, O_NONBLOCK
      // TODO: Add as oflags state to filp on open() or any vnode creation
      return -EINVAL;
      
    case F_SETFL:	/* Set file flags */
      klog_info("Fcntl F_SETFL unimplemented");
      // TODO: Effectively open flags bit O_RW, O_APPEND, O_NONBLOCK
      return -EINVAL;
      
    default:
      klog_error("Fcntl: unknown command :  %d", cmd);
      break;
  }
  
  return -ENOSYS;
}


/* @brief   dup system call
 */
int sys_dup(int fd)
{
  int new_fd;
  struct Process *current;
  
  klog_info("sys_dup(%d)", fd);
  
  current = get_current_process();

  new_fd = dup_fd(current, fd, 0, FILEDESC_MAX, 0);
  return new_fd;
}


/* @brief   dup2 system call
 *
 * Note that this system call should be atomic between the closing of the new_fd
 * and duplicating the fd to the new_fd.
 */
int sys_dup2(int fd, int new_fd)
{
  struct Process *current;

  klog_info("sys_dup2(fd:%d, new_fd:%d)", fd, new_fd);

  current = get_current_process();

  if (fd < 0 || fd >= FILEDESC_MAX || new_fd < 0 || new_fd >= FILEDESC_MAX) {
    klog_error("sys_dup2() fd out of range: %d, -EINVAL", fd);
    return -EINVAL;
  }

  if (current->fproc.fd_table[new_fd].flags & FDF_VALID) {
    kassert(current->fproc.fd_table[new_fd].filp != NULL);
    do_close(current, new_fd);    
  }
  
  new_fd = dup_fd(current, fd, new_fd, new_fd, 0);
  return new_fd;
}


/*
 *
 */
int dup_fd(struct Process *proc, int fd, int min_fd, int max_fd, uint32_t flags)
{
  struct Filp *filp;
  struct FileDesc *filedesc;
  int new_fd;
  
  klog_info("dup_fd(fd:%d, min:%d, max:%d)", fd, min_fd, max_fd);
  
  filp = filp_get(proc, fd);
  
  if (filp == NULL) {
    klog_error("dup_fd() filp is null, -EINVAL");
    return -EINVAL;
  }
  
  new_fd = fd_alloc(proc, min_fd, max_fd, &filedesc);
  
  if (new_fd < 0) {
    klog_error("dup_fd() fd_alloc failed, -EMFILE");
    return -EMFILE;
  }
  
  filedesc->filp = filp;
  
  filp_ref(filp);
  
  filedesc->flags |= FDF_VALID | flags;

  klog_info("dup_fd(%d) new_fd = %d, new_fd_flags:%08x", fd, new_fd, filedesc->flags);

  return new_fd;
}


/* @brief   Mark entry in file descriptor table as in use
 *
 */
int fd_alloc(struct Process *proc, int min_fd, int max_fd, struct FileDesc **filedesc)
{  
  klog_info("fd_alloc(proc:%08x, min:%d, max:%d, fdesc**:%08x)", (uint32_t)proc, min_fd, max_fd, (uint32_t)filedesc);

  for (int fd=min_fd; fd <= max_fd; fd++) {
    if ((proc->fproc.fd_table[fd].flags & FDF_ALLOCED) == 0) {
      kassert((proc->fproc.fd_table[fd].flags & ~FDF_ALL) == 0);

      proc->fproc.fd_table[fd].filp = NULL;
      proc->fproc.fd_table[fd].flags = FDF_ALLOCED;
      
      *filedesc = &proc->fproc.fd_table[fd];
      return fd; 
    }
  }
  
  klog_error("fd_alloc failed");
  
  *filedesc = NULL;
  return -EMFILE;
}


/* @brief   Mark entry in file descriptor table as free
 * 
 */
int fd_free(struct Process *proc, int fd)
{
  klog_info("fd_free(proc:%08x, fd:%d)", (uint32_t)proc, fd);
  
  if (fd < 0 || fd >= FILEDESC_MAX) {
    klog_error("fd_free: fd out of range -EINVAL fd:%d", fd);
    return -EINVAL;
  }

  
  kassert((proc->fproc.fd_table[fd].flags & ~FDF_ALL) == 0);
  kassert((proc->fproc.fd_table[fd].flags & FDF_ALLOCED) != 0);

  proc->fproc.fd_table[fd].filp = NULL;
  proc->fproc.fd_table[fd].flags = 0;
  
  return 0;
}


/* @brief   Mark entry in file descriptor table as free
 * 
 */
int fd_invalidate(struct Process *proc, int fd)
{
  klog_info("fd_invalidate(proc:%08x, fd:%d)", (uint32_t)proc, fd);
  
  if (fd < 0 || fd >= FILEDESC_MAX) {
    klog_error("fd_free: fd out of range -EINVAL fd:%d", fd);
    return -EINVAL;
  }


  kassert((proc->fproc.fd_table[fd].flags & ~FDF_ALL) == 0);
  kassert((proc->fproc.fd_table[fd].flags & FDF_ALLOCED) != 0);

  proc->fproc.fd_table[fd].flags &= ~FDF_VALID;
  
  return 0;
}


/* @brief   Fork the filesystem state of one process into another
 * 
 */
int fork_fds(struct Process *newp, struct Process *oldp)
{
  int sc;
  
  klog_info("**** fork_fds(newp:%08x, oldp:%08x)", (uint32_t)newp, (uint32_t)oldp);
  
  if ((sc = init_fproc(newp)) != 0) {
    klog_info("init_fproc failed in fork_fds, sc:%d", sc);
    return sc;
  }
  
  newp->fproc.umask = oldp->fproc.umask;  
  newp->fproc.current_dir = oldp->fproc.current_dir;
    
  if (newp->fproc.current_dir != NULL) {
    vnode_ref(newp->fproc.current_dir);
  }

  newp->fproc.root_dir = oldp->fproc.root_dir;

  if (newp->fproc.root_dir != NULL) {
    vnode_ref(newp->fproc.root_dir);
  }

  for (int fd = 0; fd < FILEDESC_MAX; fd++) {          
    if (oldp->fproc.fd_table[fd].flags & FDF_VALID) {
      kassert(oldp->fproc.fd_table[fd].filp != NULL);
      
      newp->fproc.fd_table[fd].filp = oldp->fproc.fd_table[fd].filp;      
      newp->fproc.fd_table[fd].flags = oldp->fproc.fd_table[fd].flags;

      kassert((newp->fproc.fd_table[fd].flags & ~FDF_ALL) == 0);
      
      filp_ref(newp->fproc.fd_table[fd].filp);

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
  klog_info("exec_fds(proc:%08x)", (uint32_t)proc);

  for (int fd = 0; fd < FILEDESC_MAX; fd++) {
    if (proc->fproc.fd_table[fd].flags & FDF_CLOSE_ON_EXEC) {
      klog_info("close on exec, fd:%d", fd);
      proc->fproc.fd_table[fd].flags &= ~FDF_CLOSE_ON_EXEC;
      do_close(proc, fd);      
    }
  }

  

  return 0;
}


