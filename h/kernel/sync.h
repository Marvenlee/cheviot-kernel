#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H


#include <kernel/lists.h>
#include <kernel/types.h>


struct Thread;


/* @brief   Kernel condition variable for TaskSleep()/TaskWakeup() calls
 */
struct Rendez
{
  LIST(Thread, blocked_list);
};


// TaskSleepInterruptible and TaskSleepTimeout interruptible flags
#define INTRF_SIGNAL  (1<<0)
#define INTRF_EVENT   (1<<1)
#define INTRF_CANCEL  (1<<2)
#define INTRF_TIMER   (1<<3)
#define INTRF_NONE    0
#define INTRF_ALL     (INTRF_SIGNAL | INTRF_EVENT | INTRF_CANCEL | INTRF_TIMER)



/* @brief   Kernel mutex, to eventually replace the big kernel lock on syscall entry
 */
#if 0 
struct Mutex
{
  int locked;
  struct Process *owner;
  LIST(Process, blocked_list);
};
#endif


#endif

