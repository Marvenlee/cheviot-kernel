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

//#define KDEBUG 1

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/interrupt.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <sys/privileges.h>


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
int sys_addinterruptserver(int irq, int event)
{
  struct Process *current_proc;
  struct Thread *current_thread;
  struct ISRHandler *isrhandler;
  int_state_t int_state;
    
  Info("sys_addinterruptserver(irq:%d, event:%d", irq, event);
  
  current_proc = get_current_process();
  current_thread = get_current_thread();
    
  if (check_privileges(current_proc, PRIV_INTERRUPT) != 0) {
    Error("* cannot add interrupt, IO not allowed");
    return -EPERM;
  }

  irq += BASE_USER_IRQ;

  if (irq < 0 || irq > NIRQ) {
    Error("* cannot add interrupt, irq range");
    return -EINVAL;
  }
  
  isrhandler = alloc_isrhandler();
  
  if (isrhandler == NULL) {
    Error("* cannot add interrupt, non free");
    return -ENOMEM;
  }
  
  isrhandler->irq = irq;
  isrhandler->thread = current_thread;
  isrhandler->event = event;

  LIST_ADD_TAIL(&current_thread->isr_handler_list, isrhandler, thread_isr_handler_link);

  int_state = DisableInterrupts();

  LIST_ADD_TAIL(&isr_handler_list[irq], isrhandler, isr_handler_entry);

  irq_handler_cnt[irq]++;
  irq_mask_cnt[irq]++;

  if (irq_mask_cnt > 0) {
    disable_irq(irq);
  }

  RestoreInterrupts(int_state);

  return isrhandler_to_isrid(isrhandler);
}


/*
 * TODO: We should automatically remove interrupt servers when terminating a thread
 */
int sys_reminterruptserver(int isrid)
{
  struct Process *current_proc;
  struct Thread *current_thread;
  struct ISRHandler *isrhandler;
  
  current_proc = get_current_process();
  current_thread = get_current_thread();

  isrhandler = LIST_HEAD(&current_thread->isr_handler_list);

  if (isrhandler == NULL) {
    return -ENOENT;
  }
  
  while(isrhandler != NULL) {
    if (isrhandler_to_isrid(isrhandler) == isrid) {
      do_free_isrhandler(current_proc, current_thread, isrhandler);
      break;
    }
    
    isrhandler = LIST_NEXT(isrhandler, thread_isr_handler_link);
  }
  
  return 0;
}


void do_free_all_isrhandlers(struct Process *proc, struct Thread *thread)
{
  struct ISRHandler *isrhandler;
  
  while ((isrhandler = LIST_HEAD(&thread->isr_handler_list)) != NULL) {
    do_free_isrhandler(proc, thread, isrhandler);    
  }
}


/*
 * TODO: Called by reminterruptserver() and thread_exit()/sys_exit() to free an interrupt handler.
 * If there are no handlers for a given IRQ the interrupt is masked.
 *
 */
int do_free_isrhandler(struct Process *proc, struct Thread *thread, struct ISRHandler *isrhandler)
{
  int irq;
  int_state_t int_state;
  
  if (thread->process != proc || isrhandler->thread != thread) {
    return -EINVAL;
  }
  
  irq = isrhandler->irq;

  int_state = DisableInterrupts();

  LIST_REM_ENTRY(&thread->isr_handler_list, isrhandler, thread_isr_handler_link);

  LIST_REM_ENTRY(&isr_handler_list[irq], isrhandler, isr_handler_entry);
  irq_handler_cnt[irq]--;

  if (irq_handler_cnt[irq] == 0) {
    irq_mask_cnt[irq] = 0;
    disable_irq(irq);
  }

  RestoreInterrupts(int_state);

  free_isrhandler(isrhandler);
  return 0;
}


/*
 *
 */
int isrhandler_to_isrid(struct ISRHandler *isrhandler)
{
  return isrhandler - isr_handler_table;
}


/*
 *
 */
struct ISRHandler *isrid_to_isrhandler(int isrid)
{
  return &isr_handler_table[isrid];
}


/* @brief   Mask an IRQ.
 *
 */
int sys_maskinterrupt(int irq)
{
  int_state_t int_state;
  struct Process *current;

  current = get_current_process();
  if (check_privileges(current, PRIV_INTERRUPT) != 0) {
    return -EPERM;
  }

  if (irq < 0 || irq >= NIRQ-BASE_USER_IRQ) {
    return -EINVAL;
  }

  irq += BASE_USER_IRQ;

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

  if (check_privileges(current, PRIV_INTERRUPT) != 0) {
    return -EPERM;
  }

  if (irq < 0 || irq >= NIRQ - BASE_USER_IRQ) {
    return -EINVAL;
  }

  irq += BASE_USER_IRQ;

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
    isr_thread_event_signal(isr_handler->thread, isr_handler->event);
    isr_handler = LIST_NEXT(isr_handler, isr_handler_entry);
  }

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

  LIST_ADD_HEAD(&isr_handler_free_list, isrhandler, free_link);
}


