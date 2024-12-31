#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H


#include <kernel/lists.h>
#include <kernel/types.h>
#include <kernel/kqueue.h>


// Forward declarations
struct Process;
struct ISRHandler;

// List types
LIST_TYPE(ISRHandler, isr_handler_list_t, isr_handler_link_t);

/*
 * @brief   Interrupt handler callback
 */
struct ISRHandler
{
  isr_handler_link_t free_link;
  isr_handler_link_t isr_handler_entry;   // Entry within a shared IRQ

  struct Thread *thread;
  isr_handler_link_t thread_isr_handler_link;

  int irq;
  int event;                    // event to send to thread on interrupt

  int priority;
  int flags;
    
  knote_list_t knote_list;      // List of knotes waiting on this ISR.
};


// Prototypes
int sys_addinterruptserver(int irq, int event);
int sys_reminterruptserver(int isr);
void do_free_all_isrhandlers(struct Process *proc, struct Thread *thread);
int do_free_isrhandler(struct Process *proc, struct Thread *thread, struct ISRHandler *isrhandler);
int isrhandler_to_isrid(struct ISRHandler *isrhandler);
struct ISRHandler *isrid_to_isrhandler(int isrid);
int sys_maskinterrupt(int irq);
int sys_unmaskinterrupt(int irq);
int interrupt_server_broadcast_event(int irq);
struct ISRHandler *alloc_isrhandler(void);
void free_isrhandler(struct ISRHandler *isrhandler);

// HAL Board Specific Functions
void enable_irq(int irq);
void disable_irq(int irq);



#endif
