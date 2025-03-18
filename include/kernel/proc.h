#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include <kernel/arch.h>
#include <kernel/lists.h>
#include <kernel/msg.h>
#include <kernel/interrupt.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <kernel/signal.h>
#include <kernel/sync.h>
#include <kernel/filesystem.h>
#include <sys/execargs.h>
#include <sys/interrupts.h>
#include <time.h>


// Forward declarations
struct Process;
struct Thread;
struct VNode;
struct Filp;
struct Session;
struct Pgrp;
struct PidDesc;
struct KQueue;


// Process related list types
LIST_TYPE(PidDesc, piddesc_list_t, piddesc_link_t);
LIST_TYPE(Session, session_list_t, session_link_t);
LIST_TYPE(Pgrp, pgrp_list_t, pgrp_link_t);
LIST_TYPE(Process, process_list_t, process_link_t);
LIST_TYPE(Thread, thread_list_t, thread_link_t);
CIRCLEQ_TYPE(Thread, thread_circleq_t, thread_cqlink_t);


// User and Group ID related
#define SUPERUSER                     0   // uid of the superuser
#define NGROUPS                       8   // Number of supplementary groups a user belongs to

// Process.state
#define PROC_STATE_FREE               0
#define PROC_STATE_INIT               111
#define PROC_STATE_ALIVE              222
#define PROC_STATE_EXITED             333

// process.flags
#define PROCF_KERNEL   (1 << 1)
#define PROCF_ALLOW_IO (1 << 1)   // Interrupts, Phys mem mapping and other IO operations allowed.

// Max number of processes and per-process resources

#define INVALID_PID       -1
#define NPROCESS          256
#define NTHREAD           256
#define NR_ISR_HANDLER    64
#define MAX_TIMER         8

#define PROC_BASENAME_SZ  16	// Characters in a process's stored basename


/* @brief   Process state
 */
struct Process
{
  pid_t pid;              // Process ID
  pid_t sid;              // Session ID
  pid_t pgid;             // Process Group ID
  process_link_t session_link;
  process_link_t pgrp_link;

  uint32_t flags;         // Permissions and features of a process
  int state;              // Process state  (alive, zombie ?)
  
  int uid;                // real UID
  int gid;                // real GID
  int euid;               // effective UID
  int egid;               // effective GID
  int suid;               // saved UID
  int sgid;               // saved GID
  
  int ngroups;            // Number of supplementary groups
  int groups[NGROUPS];

  process_link_t free_link;
  
  struct Process *parent;
  struct Rendez child_list_rendez;
  process_list_t child_list;
  process_link_t child_link;

  struct Rendez thread_list_rendez;  
  thread_list_t thread_list;
  
  struct AddressSpace as;
  
  struct Rendez rendez;             // What is this used for now in a process ? 
                                    // signals? process termination?
    
  struct ProcSignalState signal;    // For async external signals from sys_kill()
  thread_list_t unmasked_signal_thread_list;
  
  struct Timer alarm;               // TODO: Is alarm() per thread or per process?
  
  int log_level;                    // TODO: Control log level per process in kernel?  sys_setloglevel
	char basename[PROC_BASENAME_SZ];

  int exit_status;                  // sys_exit() saved error code
  bool exit_in_progress;            // A thread has initiated the exit steps
          
  struct FProcess *fproc;           // Process's file descriptor table

  uint64_t privileges;              // Privilege bitmap of process
  uint64_t privileges_after_exec;   // Privilege bitmap to use after exec.  

  futex_list_t futex_list;
};


// thread.state

// TODO: Use some random numbers (so we can hopefully detect any accidental writes)
#define THREAD_STATE_FREE             0
#define THREAD_STATE_INIT             333
#define THREAD_STATE_READY            444
#define THREAD_STATE_RUNNING          555
#define THREAD_STATE_RENDEZ_BLOCKED   777
#define THREAD_STATE_BKL_BLOCKED      888
#define THREAD_STATE_EXITED           999

// thread.flags
#define THREADF_USER     0            // User process
#define THREADF_KERNEL   (1 << 0)     // Kernel task: For kernel daemons

// thread.sched_policy
#define SCHED_IDLE -1                 // Scheduling policy not defined in C library

// Scheduler related
#define SCHED_QUANTA_JIFFIES  5       // Reschedule if task has ran for this many timer ticks


/*
 *
 */
struct Thread
{
  struct TaskCatch catch_state;
  struct CPU *cpu;
  struct ExceptionState exception_state;
  void *context;
  void *stack;                  // Kernel stack  
  void *user_stack;             // User-mode stack
  size_t user_stack_sz;         // User-mode stack size
  void *user_tcb;               // User-mode thread-control-block
  pid_t tid;

  struct Process *process;
  struct Thread *joiner_thread; // Thread that is performing a join() on this thread
  intptr_t exit_status;
  bool detached;
  
  thread_link_t free_link;
  thread_link_t thread_link;
  
  char basename[PROC_BASENAME_SZ];
  
  uint32_t flags;               // Features of a thread
  int state;                    // thread state
  
  struct Rendez rendez;         // Is this used for timers, message reply ports, joining threads ?
  
  struct Rendez *blocking_rendez;
  thread_link_t blocked_link;

  futex_list_t futex_list;

  thread_cqlink_t sched_entry;  // run-queue entry
  int sched_policy;
  int quanta_used;              // number of ticks the process has run without blocking
  int priority;                 // effective priority of process
  int desired_priority;         // default priority of process

  // TODO: Remove events, use sigsuspend() to temporarilly unmask and wait for signals.
  uint32_t intr_flags;          // Mask of what sources can interrupt TaskSleepInterruptible
  uint32_t kevent_event_mask;   // Allowed wakeup events in kevent().
  uint32_t pending_events;
  uint32_t event_mask;

  struct KNote *event_knote;
  struct KQueue *event_kqueue;
  knote_list_t knote_list;

  struct MsgPort reply_port;
  struct Msg *msg;

  struct ThreadSignalState signal;
  thread_link_t unmasked_signal_thread_link;
    
  struct Timer sleep_timer;     // Merge these 2 timers?
  struct Timer timeout_timer;
  
  isr_handler_list_t isr_handler_list;

  uint64_t usage_usec;                // CPU usage
  uint64_t last_resched_time_usec;
  uint64_t creation_usec;
};


/*
 *
 */
struct Session
{
  session_link_t free_link;    
  pid_t sid;
  struct VNode *controlling_tty;
  pid_t foreground_pgrp;
  process_list_t process_list;
};


/*
 *
 */
struct Pgrp
{
  pgrp_link_t free_link;  
  pid_t sid;
  pid_t pgid;  
  process_list_t process_list; 
};


/*
 * @brief   Process, thread, pgrp or session identifier
 */
struct PidDesc
{
  piddesc_link_t free_link;
  struct Process *proc;
  struct Thread *thread;
  struct Session *session;
  struct Pgrp *pgrp;
};



/*
 * Function Prototypes
 */

// proc/id.c
uid_t sys_getuid(void);
gid_t sys_getgid(void);
uid_t sys_geteuid(void);
gid_t sys_getegid(void);
int sys_setuid(uid_t uid);
int sys_setgid(gid_t gid);
int sys_seteuid(uid_t uid);
int sys_setegid(gid_t gid);
int sys_issetugid(void);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, uid_t egid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int getresgid(uid_t *rgid, uid_t *egid, uid_t *sgid);
int sys_setgroups(int ngroups, const gid_t *grouplist);
int sys_getgroups(int gidsetsize, gid_t *grouplist);
void init_ids(struct Process *proc);
void fork_ids(struct Process *new_proc, struct Process *old_proc);
bool is_superuser(struct Process *proc);

// proc/pid.c
pid_t sys_getpid(void);
pid_t sys_getppid(void);
pid_t sys_thread_gettid(void);
int sys_setsid(void);
int sys_getsid(pid_t pid);
int sys_setpgrp(void);
pid_t sys_getpgrp(void);
pid_t get_current_pid(void);
pid_t get_current_tid(void);
struct Process *get_process(pid_t pid);
struct Thread *get_thread(pid_t tid);
struct Process *get_thread_process(struct Thread *thread);
pid_t get_process_pid(struct Process *proc);
pid_t get_thread_tid(struct Thread *thread);
struct PidDesc *get_piddesc(struct Process *proc);
struct PidDesc *pid_to_piddesc(pid_t pid);
pid_t piddesc_to_pid(struct PidDesc *pid);

pid_t alloc_pid_proc(struct Process *proc);
pid_t alloc_pid_thread(struct Thread *thread);
void free_pid(pid_t pid);

struct Process *get_current_process(void);
struct Thread *get_current_thread(void);

void init_session_pgrp(struct Process *proc);
void fork_session_pgrp(struct Process *new_proc, struct Process *old_proc);
void fini_session_pgrp(struct Process *proc);

struct Session *alloc_session(void);
void free_session(struct Session *session);
struct Pgrp *alloc_pgrp(void);
void free_pgrp(struct Pgrp *pgrp);
struct Session *get_session(pid_t sid);
struct Pgrp *get_pgrp(pid_t pgid);
void remove_from_pgrp(struct Process *proc);
void remove_from_session(struct Process *proc);

// proc/privileges.c
int sys_set_privileges(int when, uint64_t *set, uint64_t *result);
int check_privileges(struct Process *proc, uint64_t map);
void fork_privileges(struct Process *new_proc, struct Process *parent);
void init_privileges(struct Process *proc);
void exec_privileges(struct Process *proc);

// proc/proc.c
int sys_fork(void);
void sys_exit(int status);
int sys_waitpid(int handle, int *status, int options);
struct Process *do_create_process(void (*entry)(void *), void *arg, int policy, int priority,
                               bits32_t flags, char *basename, struct CPU *cpu);
void detach_child_processes(struct Process *proc);
struct Process *alloc_process(struct Process *parent, uint32_t flags, char *name);
void free_process(struct Process *proc);
struct Process *alloc_process_struct(void);
void free_process_struct(struct Process *proc);

// proc/rwlock.c
int rwlock(struct RWLock *lock, int flags);
int rwlock_init(struct RWLock *lock);

// proc/sched.c
void SchedLock(void);
void SchedUnlock(void);
void Reschedule(void);
void SchedReady(struct Thread *thread);
void SchedUnready(struct Thread *thread);
int init_schedparams(struct Thread *thread, int policy, int priority);
int dup_schedparams(struct Thread *thread, struct Thread *old_thread);

// proc/sleep_wakeup_bkl.c
void InitRendez(struct Rendez *rendez);
void TaskSleep(struct Rendez *rendez);
int TaskSleepInterruptible(struct Rendez *rendez, struct timespec *ts, uint32_t intr_flags);
void TaskWakeup(struct Rendez *rendez);
void TaskWakeupAll(struct Rendez *rendez);
void TaskWakeupSpecific(struct Thread *thread, uint32_t intr_reason);
int TaskCheckInterruptible(struct Thread *thread, uint32_t intr_flags);
void KernelLock(void);
void KernelUnlock(void);
bool IsKernelLocked(void);
void thread_start(struct Thread *thread);
void thread_stop(void);

// proc/thread.c
pid_t sys_thread_create(void (*entry)(void *), void *arg, pthread_attr_t *_attr, void *user_tcb);
int sys_thread_join(pid_t tid, intptr_t *retval);
void sys_thread_exit(intptr_t retval);

struct Thread *fork_thread(struct Process *new_proc, struct Process *old_proc, struct Thread *old_thread);
struct Thread *create_kernel_thread(void (*entry)(void *), void *arg, int policy, int priority,
                                    uint32_t flags, struct CPU *cpu, char *name);
struct Thread *do_create_thread(struct Process *new_proc, 
                                void (*entry)(void *), void (*user_entry)(void *), 
                                void *arg,
                                int policy, int priority, 
                                uint32_t flags, int detached,
                                void *user_stack_base, size_t user_stack_sz,
                                void *user_tcb,
                                uint32_t sig_mask,
                                struct CPU *cpu, char *name);
void init_thread(struct Thread *thread, struct CPU *cpu, struct Process *proc, void *stack,
                 pid_t tid, uint32_t sig_mask, int detached, char *name);
int do_kill_thread(struct Thread *thread, int signal);
void do_kill_other_threads_and_wait(struct Process *current, struct Thread *current_thread);
int do_exit_thread(intptr_t status);
int do_join_thread(struct Thread *thread, intptr_t *status);
struct Thread *alloc_thread(struct Process *proc);
void free_thread(struct Thread *thread);
struct Thread *alloc_thread_struct(void);
void free_thread_struct(struct Thread *thread);
void thread_reaper_task(void *arg);
void set_user_stack_tcb(struct Thread *thread, void *user_stack, size_t user_stack_sz, void *user_tcb);
void get_user_stack_tcb(struct Thread *thread, void **user_stack, size_t *user_stack_sz, void **user_tcb);

// proc/thread_events.c
uint32_t sys_thread_event_check(uint32_t event_mask);
uint32_t sys_thread_event_wait(uint32_t event_mask);
int sys_thread_event_signal(int tid, int event);
int isr_thread_event_signal(struct Thread *thread, int event);

// proc/usage.c
int sys_get_cpu_usage(void *buf, size_t sz);

// Architecture-specific
void GetContext(uint32_t *context);
int SetContext(uint32_t *context);

int arch_init_fork_thread(struct Process *proc, struct Process *current_proc, 
                      struct Thread *thread, struct Thread *current_thread);
void arch_init_exec_thread(struct Process *proc, struct Thread *thread, void *entry_point,
                  void *stack_pointer, struct execargs *args);
void arch_init_user_thread(struct Thread *thread, void *entry_point, void *user_entry_point, void *stack_pointer, void *arg);
void arch_init_kernel_thread(struct Thread *thread, void *entry, void *arg);
void arch_stop_thread(struct Thread *thread);

int arch_clock_gettime(int clock_id, struct timespec *ts);


#endif
