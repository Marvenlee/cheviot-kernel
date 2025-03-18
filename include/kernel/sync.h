#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H

#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <kernel/lists.h>
#include <kernel/types.h>


// Constants
#define NR_FUTEX        4096      // Maximum number of futexes in the system
#define FUTEX_HASH_SZ   128       // Hash table size for looking up futex based on proc and address


// Forward declarations
struct Thread;
struct Futex;


// List types
LIST_TYPE(Futex, futex_list_t, futex_link_t);


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


/*
 *
 */
struct Futex
{
  uintptr_t uaddr;
  struct Process *proc;
  futex_link_t link;        // Link on global free futex list or process's futex list
  futex_link_t hash_link;  

  struct Rendez rendez;
  uint32_t hash;
};


// flags for futex_get
#define FUTEX_CREATE (1<<0)


// Prototypes
int sys_futex_destroy(void *uaddr);
int sys_futex_wait(void *uaddr, uint32_t val, const struct timespec *timeout, int flags);
int sys_futex_wake(void *uaddr, uint32_t n, int flags);
int sys_futex_requeue(void *uaddr, uint32_t n, void *uaddr2, uint32_t m, int flags);
struct Futex *futex_get(struct Process *proc, void *uaddr, int flags);
uint32_t futex_hash(struct Process *proc, void *uaddr);
struct Futex *futex_create(struct Process *proc, void *uaddr);
void futex_free(struct Process *proc, struct Futex *futex);
void fini_futexes(struct Process *proc);
int do_cleanup_futexes(struct Process *proc);
int lock_futex_table(void);
void unlock_futex_table(void);



#endif

