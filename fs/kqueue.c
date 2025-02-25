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
 * Event notification system for files, events and other kernel objects
 */

//#define KDEBUG

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <poll.h>
#include <string.h>
#include <kernel/proc.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/dbg.h>
#include <kernel/kqueue.h>


/* @brief   Create a kqueue object in the current process
 *
 * @return  file descriptor of created kqueue or negative errno on error
 */
int sys_kqueue(void)
{
  struct Process *current;
  int fd;

  Info("sys_kqueue");
  
  current = get_current_process();
  fd = alloc_fd_kqueue(current);

  return fd;
}


/* @brief   Register interest and wait for kernel events
 *
 * @param   fd, file descriptor of kqueue
 * @param   changelist, array of kevent structures to change the settings of 
 * @param   nchanges, number of kevents in changelist array
 * @param   eventlist, array to receive new kevents into
 * @param   nevents, size of eventlist array
 * @param   timeout, maximum time to wait to receive an event
 * @return  number of events copied into eventlist buffer or negative errno on error
 *
 * TODO: Need to prevent EVFILT_THREAD_EVENT requests from sys_kevent.
 */
int sys_kevent(int fd, const struct kevent *changelist, int nchanges,
           struct	kevent *eventlist, int nevents, const struct timespec *_timeout)
{
  int nevents_returned;
  struct kevent ev;
  struct KQueue *kqueue;
  struct KNote *knote;
  struct Process *current;
  struct Thread *current_thread;
  struct timespec timeout;
  struct timespec *timeoutp;
  int_state_t int_state;
  bool timedout = false;
  int sc = 0;
  
//  Info("sys_kevent(%d, nchanges:%d, nevents:%d", fd, nchanges, nevents);
  
  current = get_current_process();
  current_thread = get_current_thread();
  
  if (_timeout != NULL) {
    CopyIn(&timeout, _timeout, sizeof timeout);
    timeoutp = &timeout;
  } else {
    timeoutp = NULL;
  }
  
  kqueue = get_kqueue(current, fd);
  
  while (kqueue->busy == true) {
    TaskSleep(&kqueue->busy_rendez);
  }
  kqueue->busy = true;
  
  // Processing of adding/removing/enabling and disabling events.  
  if (nchanges > 0 && changelist != NULL) {
    for (int t = 0; t < nchanges; t++) {      
      CopyIn(&ev, &changelist[t], sizeof ev);
      
      if (ev.filter < 0 || ev.filter >= EVFILT_SYSCOUNT
          || ((ev.flags & (EV_ADD | EV_DELETE)) == (EV_ADD | EV_DELETE)) 
          || ((ev.flags & (EV_ENABLE | EV_DISABLE)) == (EV_ENABLE | EV_DISABLE))) {
        sc = -EINVAL;
        goto exit;      
      }
      
      knote = get_knote(kqueue, &ev);

      if (ev.flags & EV_ADD) {
        if (knote == NULL) {
          if((ev.flags & EV_DISABLE) == 0) {
            ev.flags |= EV_ENABLE;
          }
                    
          knote = alloc_knote(kqueue, &ev);
    
          if (knote == NULL) {
            sc = -ENOMEM;
            goto exit;
          }          
        } else {
          sc = -EEXIST;
          goto exit;
        }
     }
              
     if (ev.flags & EV_DELETE) {
        if (knote != NULL) {
          disable_knote(kqueue, knote);
          free_knote(kqueue, knote);
          knote = NULL;
          continue;        
        } else {
          sc = -ENOENT;
          goto exit;
        }
      }
            
      if (knote != NULL) {
        if (ev.flags & EV_ENABLE) {
          enable_knote(kqueue, knote);
        }
        
        if (ev.flags & EV_DISABLE) {
          disable_knote(kqueue, knote);
        }
      }
    }
  }
  
  // Processing of returned events.  
  nevents_returned = 0;

  if (nevents > 0 && eventlist != NULL) {
    uint32_t saved_event_mask = current_thread->event_mask;
    
    current_thread->event_mask |= current_thread->kevent_event_mask;
    sc = 0;
    
    // Prioritize events    
    while (LIST_HEAD(&kqueue->pending_list) == NULL &&
           (current_thread->pending_events & current_thread->event_mask) == 0) {
      if ((sc = TaskSleepInterruptible(&kqueue->event_rendez, timeoutp, INTRF_ALL)) != 0) {
        sc = 0;     // FIXME: Remove, need to determine if signal, timeout or event
        break;
      }        
    }

    process_event_knotes(kqueue, current_thread);

    current_thread->event_mask = saved_event_mask;
        
    if (sc == 0) {
      while (nevents_returned < nevents) {        
        knote = LIST_HEAD(&kqueue->pending_list);
              
        if (knote == NULL) {
          break;
        }

        memset(&ev, 0, sizeof ev);
        ev.ident = knote->ident;
        ev.filter = knote->filter;          
        ev.flags  = knote->flags;
        ev.fflags = knote->fflags;
        ev.udata = knote->udata;
        
        CopyOut(&eventlist[nevents_returned], &ev, sizeof ev);
        nevents_returned++;
        
        LIST_REM_HEAD(&kqueue->pending_list, pending_link);
        knote->pending = false;
        knote->on_pending_list = false;
                
        if (knote->flags & EV_ONESHOT) {
          free_knote(kqueue, knote);
        }    
      }
    }
  }

  kqueue->busy = false;
  
  if (LIST_HEAD(&kqueue->pending_list) != NULL) {
    TaskWakeup(&kqueue->busy_rendez);  
  }
  
  if (sc != 0) {
    Info("kqueue A sc:%d", sc);  
    return sc;
  }
  
  Info("kevent: events:%d", nevents_returned);
  return nevents_returned;

exit:
  // TODO: Any kevent cleanup
  Info("kqueue B sc:%d", sc);  
  return sc;
}


/* @brief   Convert a thread's caught events to a pending kevent
 * 
 * @param   kqueue, passed by kevent() and must match that set by sys_thread_event_kevent_mask
 * @param   current_thread, the current thread
 */
void process_event_knotes(struct KQueue *kqueue, struct Thread *current_thread)
{
  int_state_t int_state;
  uint32_t caught_events;    
  
  if (kqueue != current_thread->event_kqueue) {
    return;
  }
  
  if (current_thread->event_mask == 0 || current_thread->event_knote == NULL) {
    return;
  }
  
  int_state = DisableInterrupts();
  uint32_t pending_old = current_thread->pending_events;
  caught_events = (current_thread->pending_events & current_thread->event_mask);
  current_thread->pending_events &= ~caught_events;
  RestoreInterrupts(int_state);

  if (caught_events == 0) {
    return;
  }

  Info("caught:%08x, pend:%08x", caught_events, pending_old);
  current_thread->event_knote->fflags = caught_events;
  knote(&current_thread->knote_list, 0);
}


/* @brief   Send a knote event to a vnode in the kernel
 *
 * @param   fd, file descriptor of mount on which the file exists
 * @param   ino_nr, inode number of the file
 * @param   hint, hint of why this is being notified. *
 * @return  0 on success, negative errno on error
 *
 * Perhaps change it to sys_setvnodeattrs(fd, ino_nr, flags);
 */
int sys_knotei(int fd, int ino_nr, long hint)       // hint should be bitfield, add filter argument
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct Process *current;
  
  current = get_current_process();  
  sb = get_superblock(current, fd);
  
  if (sb == NULL) {
    return -EINVAL;
  }
  
  vnode = vnode_get(sb, ino_nr);
  
  rwlock(&vnode->lock, LK_SHARED);    // May not be needed if knote doesn't block
  
  if (vnode == NULL) {
    return -EINVAL;
  }
    
  knote(&vnode->knote_list, hint);  

  rwlock(&vnode->lock, LK_RELEASE);
  vnode_put(vnode);
  return 0;
}


/* @brief   Close a kqueue file descriptor
 *
 * @param   proc,
 * @param   fd,
 * @return  0 on success, negative errno on error
 */
int close_kqueue(struct Process *proc, int fd)
{
  // Delete all Notes on list
  
  free_fd_kqueue(proc, fd);
  return 0;
}


/* @brief   Add an event note to a kqueue's pending list
 *
 * @param   knote_list, list of knotes attached to an object
 * @param   hint, a hint as to why the knote was added   TODO: Should be filter
 * @return  0 on success, negative errno on error
 */
int knote(knote_list_t *knote_list, int hint)    // hint should be bitfield, add filter argument
{
  struct KNote *knote;
  struct KQueue *kqueue;

  knote = LIST_HEAD(knote_list);
 
  Info("knote(list:%08x, hint:%d) knote:%08x", (uint32_t)knote_list, hint, (uint32_t)knote);
  
  while(knote != NULL) {
    knote->pending = true;
    knote->hint = hint;

    if (knote->enabled == true && knote->on_pending_list == false) {        
      kqueue = knote->kqueue;      
      LIST_ADD_TAIL(&kqueue->pending_list, knote, pending_link);
      knote->on_pending_list = true;
      
      TaskWakeup(&kqueue->event_rendez);
    }
    
    knote = LIST_NEXT(knote, object_link);
  }
    
  return 0;
}


/* @brief   Remove event notes to a kqueue's pending list
 *
 * @param   knote_list, list of knotes attached to an object
 * @param   hint, a hint as to why the knote was added
 * @return  0 on success, negative errno on error
 */
int knote_dequeue(knote_list_t *knote_list, int filter)    // add hint, should be bitfield
{
  struct KNote *knote;
  struct KQueue *kqueue;

  knote = LIST_HEAD(knote_list);
  
  Info("knote_dequeue(list:%08x, filter:%d) knote:%08x", (uint32_t)knote_list, filter, (uint32_t)knote);
  
  while(knote != NULL) {
    if (knote->filter == filter) {
      if (knote->enabled == true && knote->on_pending_list == true) {        
        kqueue = knote->kqueue;
        LIST_REM_ENTRY(&kqueue->pending_list, knote, pending_link);
        knote->pending = false;
        knote->hint = 0;
        knote->on_pending_list = false;
      }
    }
        
    knote = LIST_NEXT(knote, object_link);
  }
    
  return 0;
}



/*
 *
 */
struct KNote *get_knote(struct KQueue *kq, struct kevent *ev)
{
  uint16_t hash;
  struct KNote *knote;
  
  hash = knote_calc_hash(kq, ev->ident, ev->filter);
  knote = LIST_HEAD(&knote_hash[hash]);
  
  while (knote != NULL) {
    if (knote->kqueue == kq && knote->ident == ev->ident && knote->filter == ev->filter) {
      return knote;
    }

    knote = LIST_NEXT(knote, hash_link);
  }
  
  return NULL;  
}


/*
 *
 */
struct KQueue *get_kqueue(struct Process *proc, int fd)
{
  struct Filp *filp;
  
  filp = get_filp(proc, fd);
  
  if (filp == NULL) {
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_KQUEUE) {
    return NULL;
  }

  return filp->u.kqueue;
}


/* @brief   Allocate a kqueue and associated file descriptor
 *
 * Allocates a handle structure.  Checks to see that free_handle_cnt is
 * non-zero should already have been performed prior to calling alloc_fd().
 */
int alloc_fd_kqueue(struct Process *proc)
{
  int fd;
  struct KQueue *kqueue;
  
  fd = alloc_fd_filp(proc);
  
  if (fd < 0) {
    return -EMFILE;
  }
  
  kqueue = alloc_kqueue();
  
  if (kqueue == NULL) {
    free_fd_filp(proc, fd);
    return -EMFILE;
  }
  
  kqueue->reference_cnt=1;
  set_fd(proc, fd, FILP_TYPE_KQUEUE, 0, kqueue);
  
  return fd;
}


/* @brief   Free a kqueue and associated file descriptor
 *
 * Marks the file descriptor as free, decrements reference counts
 * and if the kqueue is no longer referenced it is freed.
 */
int free_fd_kqueue(struct Process *proc, int fd)
{
  struct KQueue *kqueue;
  
  Info("free_fd_kqueue");
  
  kqueue = get_kqueue(proc, fd);

  if (kqueue == NULL) {
    return -EINVAL;
  }

  free_fd_filp(proc, fd);
  free_kqueue(kqueue);
  return 0;
}


/*
 *
 */
struct KQueue *alloc_kqueue(void)
{
  struct KQueue *kqueue;

  kqueue = LIST_HEAD(&kqueue_free_list);

  if (kqueue == NULL) {
    return NULL;
  }
  
  LIST_REM_HEAD(&kqueue_free_list, free_link);

  kqueue->busy = false;
  kqueue->reference_cnt = 0;

  InitRendez(&kqueue->busy_rendez);
  InitRendez(&kqueue->event_rendez);
  
  LIST_INIT(&kqueue->knote_list);
  LIST_INIT(&kqueue->pending_list);
  
  return kqueue;
}


/*
 *
 */
void free_kqueue(struct KQueue *kqueue)
{
  Info("free_kqueue(kq:%08x)", (uint32_t)kqueue);
  
  kqueue->reference_cnt--;
  
  if (kqueue->reference_cnt == 0) {
    // TODO: We need to detach any knotes from the kqueue before freeing it.
    LIST_ADD_HEAD(&kqueue_free_list, kqueue, free_link);
  }
}


/*
 *
 */
struct KNote *alloc_knote(struct KQueue *kqueue, struct kevent *ev)
{
  int sc = 0;
  int hash;
  struct KNote *knote;
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct Process *current;
  struct Thread *thread;
  
  current = get_current_process();
  
  Info ("alloc_knote(kq:%08x, ev:%08x)", (uint32_t)kqueue, (uint32_t)ev);
  
  if ((knote = LIST_HEAD(&knote_free_list)) == NULL) {
    return NULL;
  }
  
  LIST_REM_HEAD(&knote_free_list, link);  
  
  Info ("new knote addr: %08x", (uint32_t)knote);
  
  memset(knote, 0, sizeof *knote);
  
  knote->kqueue = kqueue;
  knote->ident = ev->ident;  
  knote->filter = ev->filter;          
  knote->flags = ev->flags;
  knote->fflags = ev->fflags;
  knote->data = NULL;
  knote->udata = ev->udata;
 
 
  Info("alloc knote->kqueue = %08x", (uint32_t)kqueue);
  Info("alloc knote->ident = %d", ev->ident);
  Info("alloc knote->filter = %d", ev->filter);
  Info("alloc knote->flags = %08x", ev->flags);
  Info("alloc knote->fflags = %08x", ev->fflags);
  
    
  knote->enabled = false;
  knote->pending = false;
  knote->on_pending_list = false;  // FIXME: Can't we get this if pending and enabled is true ?
  
  knote->object = NULL;
    
  switch (knote->filter) {
    case EVFILT_READ:
    case EVFILT_WRITE:
    // TODO: Add EVFILT_FS for mount/unmount events    
    case EVFILT_VNODE:
      vnode = get_fd_vnode(current, knote->ident);
      
      if (vnode) {
        knote->object = vnode;
        LIST_ADD_TAIL(&vnode->knote_list, knote, object_link);
      } else {
        sc = -EINVAL;
      }
      break;
      
    case EVFILT_AIO:
      sc = -ENOSYS;
      break;

    case EVFILT_PROC:
      sc = -ENOSYS;
      break;
      
    case EVFILT_SIGNAL:    
      sc = -ENOSYS;
      break;
      
    case EVFILT_TIMER:
      sc = -ENOSYS;
      break;
      
    case EVFILT_NETDEV:
      sc = -ENOSYS;
      break;
      
    case EVFILT_USER:
      sc = -ENOSYS;
      break;
      
    case EVFILT_MSGPORT:
      sb = get_superblock(current, knote->ident);
    
      if (sb) {
        knote->object = sb;
        LIST_ADD_TAIL(&sb->msgport.knote_list, knote, object_link);
      } else {
        Info("alloc kn EVFILT_MSGPORT - no superblock");
        sc = -EINVAL;
      }
      
      break;

    case EVFILT_THREAD_EVENT:
      thread = get_thread(knote->ident);
      
      if (thread) {
        knote->object = thread;
        LIST_ADD_TAIL(&thread->knote_list, knote, object_link);
      } else {
        Info("alloc kn EVFILT_THREAD_EVENT - no thread");
        sc = -EINVAL;
      }
      break;
      
    default:
      sc = -ENOSYS;

      // kernel panic  
  }

  if (sc != 0) {
    free_knote(kqueue, knote);
    return NULL;
  }

  LIST_ADD_TAIL(&kqueue->knote_list, knote, kqueue_link);  
  hash = knote_calc_hash(kqueue, knote->ident, knote->filter);
  LIST_ADD_TAIL(&knote_hash[hash], knote, hash_link);

  return knote;
}


/* @brief   Free a knote
 *
 * @param   kqueue,
 * @param   knote,
 *
 * Removes the knote from the kqueue and the object's knote list 
 */
void free_knote(struct KQueue *kqueue, struct KNote *knote)
{
  int hash;
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct Process *current;
  struct Thread *thread;
  
  Info("free_knote(kq:%08x, kn:%08x", (uint32_t)kqueue, (uint32_t)knote);
  
  current = get_current_process();
  
  switch (knote->filter) {
    case EVFILT_READ:
    case EVFILT_WRITE:
    // TODO: ADd EVFILT_FS for mount/unmount events
    case EVFILT_VNODE:
      vnode = get_fd_vnode(current, knote->ident);
      
      if (vnode) {
        knote->object = NULL;
        LIST_REM_ENTRY(&vnode->knote_list, knote, object_link);
      }
      break;
      
    case EVFILT_AIO:
      break;

    case EVFILT_PROC:
      break;
      
    case EVFILT_SIGNAL:    
      break;
                
    case EVFILT_TIMER:
      break;
      
    case EVFILT_NETDEV:
      break;
      
    case EVFILT_USER:
      break;

    case EVFILT_MSGPORT:
      sb = get_superblock(current, knote->ident);
      
      if (sb) {
        knote->object = NULL;
        LIST_REM_ENTRY(&sb->msgport.knote_list, knote, object_link);
      }
      break;

    case EVFILT_THREAD_EVENT:
      thread = get_thread(knote->ident);

       Info("free kn EVFILT_THREAD_EVENT thread:%08x", (uint32_t)thread);
      
      if (thread != NULL) {
        knote->object = NULL;
        LIST_REM_ENTRY(&thread->knote_list, knote, object_link);
      }
      break;
      
    default:
      break;
  }

  if (knote->on_pending_list == true) {
    LIST_REM_ENTRY(&kqueue->pending_list, knote, pending_link);
    knote->on_pending_list = false;
  }
  
  knote->pending = false;  
  LIST_REM_ENTRY(&kqueue->knote_list, knote, kqueue_link);  

  hash = knote_calc_hash(kqueue, knote->ident, knote->filter);
  LIST_REM_ENTRY(&knote_hash[hash], knote, hash_link);

  LIST_ADD_HEAD(&knote_free_list, knote, link);
}


/* @brief   Enable an existing knote
 *
 * TODO: Check if we need to raise any pending events,
 * e.g. if file has data in buffer for reading or
 * if message is already on message port.
 *
 * check if knote was previously on pending list, else
 * also check if object's state would raise a pending event.
 */
void enable_knote(struct KQueue *kqueue, struct KNote *knote)
{
  struct Process *current;
  struct SuperBlock *sb;
  struct VNode *vnode;
  struct MsgPort *msgport;
  
  current = get_current_process();
  
  if (knote->enabled == true) {
    return;
  }
  
  knote->enabled = true;
  
  // Check if already pending, if so add to pending queue  

  if (knote->pending == true) {
    KASSERT(knote->on_pending_list == false);
    
    LIST_ADD_TAIL(&kqueue->pending_list, knote, pending_link);
    knote->on_pending_list = true;
    return;
  }
  
  // We're not sure if an event is pending, so check.

  switch (knote->filter)
  {
    case EVFILT_READ:
    case EVFILT_WRITE:
    // TODO: ADd EVFILT_FS for mount/unmount events
    case EVFILT_VNODE:
      vnode = get_fd_vnode(current, knote->ident);
      
      if (vnode) {
      }
      break;
      
    case EVFILT_AIO:
      break;

    case EVFILT_PROC:
      break;
      
    case EVFILT_SIGNAL:    
      break;

    case EVFILT_TIMER:
      break;
      
    case EVFILT_NETDEV:
      break;
      
    case EVFILT_USER:
      break;
            
    case EVFILT_MSGPORT:
      // Check if there is already a message on port
      sb = get_superblock(current, knote->ident);
      
      if (sb) {
        msgport = &sb->msgport;
  
        if (kpeekmsg(msgport) != NULL) {
          knote->pending = true;           
          LIST_ADD_TAIL(&kqueue->pending_list, knote, pending_link);
          knote->on_pending_list = true;
        }        
      }
      break;

    case EVFILT_THREAD_EVENT:
      break;
      
    default:
      break;
  }
}


/* @brief   Disable an existing knote
 *
 * Remove from pending list,  keep knote->pending as true if already pending
 */
void disable_knote(struct KQueue *kqueue, struct KNote *knote)
{
  if (knote->enabled == false) {
    return;
  }
  
  knote->enabled = false;
  
  if (knote->on_pending_list == true) {
    LIST_REM_ENTRY(&kqueue->pending_list, knote, pending_link);
    knote->on_pending_list = false;
  }
}


/* @brief   Calculate a hash value for looking up a knote
 */
int knote_calc_hash(struct KQueue *kq, int ident, int filter)
{
  return ((ident << 8) | filter) % KNOTE_HASH_SZ;
}

