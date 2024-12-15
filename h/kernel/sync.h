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



/* @brief   Shared/exclusive Reader Writer Lock
 *
 */
struct RWLock
{
  struct Rendez rendez;
  int share_cnt;
  int exclusive_cnt;
  int is_draining;
};

// rwlock() flags masks
#define LOCK_REQUEST_MASK  0x0000000F

// Lock Request types for rwlock()
#define LK_EXCLUSIVE    1
#define LK_SHARED       2
#define LK_UPGRADE      3
#define LK_DOWNGRADE    4
#define LK_RELEASE      5
#define LK_DRAIN        6


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

