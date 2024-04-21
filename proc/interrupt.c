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

/*
 * Architecture-neutral code to create and close InterruptHandler objects
 * Used by device drivers to receive notification of interrupts.
 */

#define KDEBUG 1

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>


/* @brief   Raise an kevent on an interrupt's file descriptor
 *
 * @param   fd, file descriptor to raise a kevent for
 * @return  0 on success, negative errno on failure
 *
 * The purpose of this system call is to be able to wake up a
 * main thread waiting on an interrupt file descriptor using kqueue.
 * A separate "bottom-half" thread dedicated to handling interrupts
 * waits on events sent from the interrupt handler. If an interrupt
 * thread needs to wake up the main thread, for example, to notify
 * that an operation has completed, it can call knotei() to do so.
 *
 * TODO: Can we make this more generic for other objects?
 * Or can we replace with events interrupting kevent() ?
 *
 */
int sys_knoteinterruptserver(int fd)
{
  return -ENOSYS;
}


/* @brief   Registers an interrupt notification server
 *
 * @param   irq, interrupt number
 * @param   thread_id, thread to send an event to on interrupt occuring
 * @param   event, event bit to send to thread
 * @return  file descriptor or negative errno on error
 *
 * Upon the interrupt occuring the thread with the specified thread_id
 * is notified of the event. If the target thread is calling kqueue, then
 * -EINTR is returned.  If the target thread is waiting on this event
 * with thread_event_wait() then the target thread is awakened.
 *
 * On creation of an interrupt server, the interrupt is placed in a masked
 * state. If this is a shared interrupt and is currently unmasked then
 * it will become masked.  For shared interrupts the caller of should 
 * unmask interrupts quickly to avoid starving the other users of the
 * interrupt.
 */
int sys_addinterruptserver(int irq, int thread_id, int event)
{
  struct Process *current;
  struct ISRHandler *isrhandler;
  int fd;
  int_state_t int_state;
    
  current = get_current_process();

  if (!io_allowed(current)) {
    return -EPERM;
  }

  if (irq < 0 || irq > NIRQ) {
    Error("irq out of range");
    return -EINVAL;
  }
  
  fd = alloc_fd_isrhandler(current);
  
  if (fd < 0) {
    Error("failed to alloc fd_isrhandler");  
    return -ENOMEM;
  }
  
  isrhandler = get_isrhandler(current, fd);

  isrhandler->isr_irq = irq;
  isrhandler->thread_id = thread_id;
  isrhandler->event = event;
  isrhandler->isr_process = current;
  
  int_state = DisableInterrupts();
  
  LIST_ADD_TAIL(&isr_handler_list[irq], isrhandler,
                isr_handler_entry);

  irq_handler_cnt[irq]++;    
  irq_mask_cnt[irq]++;
  
  if (irq_mask_cnt > 0) {
    disable_irq(irq);
  }
  
  RestoreInterrupts(int_state);
  return fd;
}


/* @brief   Mask an IRQ.
 *
 */
int sys_maskinterrupt(int irq)
{
  int_state_t int_state;
  struct Process *current;

  current = get_current_process();
  if (!io_allowed(current)) {
    return -EPERM;
  }

  if (irq < 0 || irq >= NIRQ) {
    return -EINVAL;
  }

  int_state = DisableInterrupts();

  irq_mask_cnt[irq]++;
  
  if (irq_mask_cnt[irq] > 0) {
    disable_irq(irq);
  }

  RestoreInterrupts(int_state);
  return irq_mask_cnt[irq];
}


/* @brief   Unmasks an IRQ
 *
 */
int sys_unmaskinterrupt(int irq)
{
  int_state_t int_state;  
  struct Process *current;

  current = get_current_process();

  if (!io_allowed(current)) {
    return -EPERM;
  }

  if (irq < 0 || irq >= NIRQ) {
    return -EINVAL;
  }

  int_state = DisableInterrupts();

  if (irq_mask_cnt[irq] > 0) {
    irq_mask_cnt[irq]--;
  }
  
  if (irq_mask_cnt[irq] == 0) {
    enable_irq(irq);
  } else {
    disable_irq(irq);  
  }

  RestoreInterrupts(int_state);
  return irq_mask_cnt[irq];
}


/* @brief   Send events to "bottom-half" threads in device drivers.
 */
int interrupt_server_broadcast_event(int irq)
{
  struct ISRHandler *isr_handler;

  isr_handler = LIST_HEAD(&isr_handler_list[irq]);

  while (isr_handler != NULL) {        
    isr_thread_event_signal(isr_handler->thread_id, isr_handler->event);
    isr_handler = LIST_NEXT(isr_handler, isr_handler_entry);
  }

  return 0;
}


/*
 * Called by CloseHandle() to free an interrupt handler.  If there are no
 * handlers for a given IRQ the interrupt is masked.
 *
 * TODO: Need to prevent forking of interrupt descriptors (dup in same process is ok)
 */
int close_isrhandler(struct Process *proc, int fd)
{
  struct ISRHandler *isrhandler;
  int irq;
  int_state_t int_state;
  
  isrhandler = get_isrhandler(proc, fd);
  
  if (isrhandler == NULL) {
    return -EINVAL;
  }

  irq = isrhandler->isr_irq;

  int_state = DisableInterrupts();

  LIST_REM_ENTRY(&isr_handler_list[irq], isrhandler, isr_handler_entry);
  irq_handler_cnt[irq]--;

  if (irq_handler_cnt[irq] == 0) {
    irq_mask_cnt[irq] = 0;
    disable_irq(irq);
  }

  RestoreInterrupts(int_state);

  free_fd_isrhandler(proc, fd);
  return 0;
}


/*
 *
 */
struct ISRHandler *get_isrhandler(struct Process *proc, int fd)
{
  struct Filp *filp;
  
  filp = get_filp(proc, fd);
  
  if (filp == NULL) {
    return NULL;
  }
  
  if (filp->type != FILP_TYPE_ISRHANDLER) {
    return NULL;
  }
  
  return filp->u.isrhandler;
}


/*
 * Allocates a handle structure.
 */
int alloc_fd_isrhandler(struct Process *proc)
{
  int fd;
  struct ISRHandler *isrhandler;
  
  fd = alloc_fd_filp(proc);
  
  if (fd < 0) {
    return -EMFILE;
  }
  
  isrhandler = alloc_isrhandler();
  
  if (isrhandler == NULL) {
    free_fd_filp(proc, fd);
    return -EMFILE;
  }
  
  set_fd(proc, fd, FILP_TYPE_ISRHANDLER, FD_FLAG_CLOEXEC, isrhandler);  
  return fd;
}


/*
 * Returns a handle to the free handle list.
 */
int free_fd_isrhandler(struct Process *proc, int fd)
{
  struct ISRHandler *isrhandler;
  
  isrhandler = get_isrhandler(proc, fd);

  if (isrhandler == NULL) {
    return -EINVAL;
  }

  free_isrhandler(isrhandler);
  free_fd_filp(proc, fd);
  return 0;
}


/*
 *
 */
struct ISRHandler *alloc_isrhandler(void)
{
  struct ISRHandler *isrhandler;

  isrhandler = LIST_HEAD(&isr_handler_free_list);

  if (isrhandler == NULL) {
    return NULL;
  }

  LIST_REM_HEAD(&isr_handler_free_list, free_link);
  isrhandler->reference_cnt = 1;
  return isrhandler;
}


/*
 *
 */
void free_isrhandler(struct ISRHandler *isrhandler)
{
  if (isrhandler == NULL) {
    return;
  }

  isrhandler->reference_cnt--;
  
  if (isrhandler->reference_cnt == 0) {
    LIST_ADD_HEAD(&isr_handler_free_list, isrhandler, free_link);
  }
}


