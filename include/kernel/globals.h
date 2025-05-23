#ifndef KERNEL_GLOBALS_H
#define KERNEL_GLOBALS_H

#include <sys/syscalls.h>
#include <kernel/types.h>
#include <kernel/interrupt.h>
#include <kernel/filesystem.h>
#include <kernel/lists.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/kqueue.h>
#include <kernel/msg.h>


/*
 * Memory
 */
extern vm_size mem_size;
extern int max_pageframe;
extern struct Pageframe *pageframe_table;

extern pageframe_list_t free_4k_pf_list;
extern pageframe_list_t free_16k_pf_list;
extern pageframe_list_t free_64k_pf_list;

extern int max_memregion;
extern struct MemRegion *memregion_table;
extern memregion_list_t unused_memregion_list;


/*
 * Timer
 */
extern struct Thread *timer_thread;
extern timer_list_t timing_wheel[JIFFIES_PER_SECOND];
extern struct Rendez timer_rendez;

// extern timer_list_t free_timer_list;
// extern int max_timer;
// extern struct Timer *timer_table;

extern volatile spinlock_t timer_slock;
extern volatile long hardclock_time;
extern volatile long softclock_time;

extern uint64_t usage_start_usec;

/*
 * Interrupt
 */
extern int nirq;
extern int irq_mask_cnt[NIRQ];
extern int irq_handler_cnt[NIRQ];
extern isr_handler_list_t isr_handler_list[NIRQ];


/*
 *
 */
extern int max_cpu;
extern int cpu_cnt;
extern struct CPU cpu_table[1];

// TODO: PID table will be fixed size (for processes, threads, pgrps and sessions)
// Threads and process structs will be dynamically allocated, with buddy allocator
// 64k blocks per process (for FD tables, threads, process page tables, kernel stacks, etc).

extern int max_process;
extern struct Process *process_table;
extern process_list_t free_process_list;

extern int max_pid;
extern struct PidDesc *pid_table;
extern piddesc_list_t free_piddesc_list;

extern struct Session *session_table;
extern session_list_t free_session_list;

extern struct Pgrp *pgrp_table;
extern pgrp_list_t free_pgrp_list;

extern int max_thread;
extern struct Thread *thread_table;
extern thread_list_t free_thread_list;

extern int max_futex;
extern struct Futex *futex_table;
extern futex_list_t free_futex_list; 
extern int futex_table_busy;
extern struct Rendez futex_table_busy_rendez;
extern futex_list_t futex_hash_table[FUTEX_HASH_SZ];

extern struct Process *root_process;
extern struct Thread *root_thread; 

/*
 * Thread reaper
 */
extern struct Thread *thread_reaper_thread;
extern thread_list_t thread_reaper_detached_thread_list;
extern struct Rendez thread_reaper_rendez;

/*
 * Scheduler
 */

extern thread_circleq_t sched_queue[32];
extern uint32_t sched_queue_bitmap;

extern int bkl_locked;
extern spinlock_t inkernel_now;
extern int inkernel_lock;
extern struct Thread *bkl_owner;
extern thread_list_t bkl_blocked_list;

/*
 * free counters (unused?)
 */
extern int free_pid_cnt;
extern int free_handle_cnt;
extern int free_process_cnt;
extern int free_channel_cnt;
extern int free_timer_cnt;

/*
 * Sockets
 */
extern int max_socket;
extern struct Socket *socket_table;

/*
 * Filesystem
 */
extern int max_superblock;
extern struct SuperBlock *superblock_table;
extern superblock_list_t free_superblock_list;
extern superblock_list_t mounted_superblock_list;
extern struct RWLock superblock_list_lock;

extern struct VNode *root_vnode;

extern int max_vnode;
extern struct VNode *vnode_table;
extern vnode_list_t vnode_free_list;
extern vnode_list_t vnode_hash[VNODE_HASH];
extern struct RWLock vnode_list_lock;

extern int max_filp;
extern struct Filp *filp_table;
extern filp_list_t filp_free_list;

extern int max_pipe;
extern struct Pipe *pipe_table;
extern pipe_list_t free_pipe_list;
extern struct SuperBlock pipe_sb;

extern int max_isr_handler;
extern struct ISRHandler *isr_handler_table;
extern isr_handler_list_t isr_handler_free_list;

extern int max_kqueue;
extern struct KQueue *kqueue_table;
extern kqueue_list_t kqueue_free_list;

extern int max_knote;
extern struct KNote *knote_table;
extern knote_list_t knote_free_list;
extern knote_list_t knote_hash[KNOTE_HASH_SZ];


/*
 * Directory Name Lookup Cache
 */
extern struct DName dname_table[NR_DNAME];
extern dname_list_t dname_lru_list;
extern dname_list_t dname_hash[DNAME_HASH];

/*
 * VNode for syslog (TODO)
 */
extern struct VNode *logger_vnode;

/*
 * File Cache
 */
extern int max_buf;
extern struct Buf *buf_table;
extern struct Rendez buf_list_rendez;
extern buf_list_t buf_hash[BUF_HASH];
extern buf_list_t buf_avail_list;
extern struct RWLock cache_lock;         // FIXME: May not be needed


#endif
