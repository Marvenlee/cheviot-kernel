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
 * Global variables related to process and thread management
 */

#include <kernel/types.h>
#include <kernel/filesystem.h>
#include <kernel/interrupt.h>
#include <kernel/proc.h>
#include <kernel/kqueue.h>
#include <kernel/msg.h>


/*
 * free counters (unused?)
 */
int free_pid_cnt;
int free_handle_cnt;
int free_process_cnt;
int free_timer_cnt;

/*
 * Processes and CPUs
 */
int max_cpu;
int cpu_cnt;
struct CPU cpu_table[MAX_CPU];

int max_process;
struct Process *process_table;
process_list_t free_process_list;

int max_pid;
struct PidDesc *pid_table;
piddesc_list_t free_piddesc_list;

struct Session *session_table;
session_list_t free_session_list;

int max_futex;
struct Futex *futex_table;
futex_list_t free_futex_list;
int futex_table_busy;
struct Rendez futex_table_busy_rendez;
futex_list_t futex_hash_table[FUTEX_HASH_SZ];



struct Pgrp *pgrp_table;
pgrp_list_t free_pgrp_list;

int max_thread;
struct Thread *thread_table;
thread_list_t free_thread_list;

struct Process *root_process;
struct thread *root_thread;

/*
 * Thread Reaper
 */
 
struct Thread *thread_reaper_thread;
thread_list_t thread_reaper_detached_thread_list;
struct Rendez thread_reaper_rendez;


 
/*
 * Scheduler
 */
thread_circleq_t sched_queue[32];
uint32_t sched_queue_bitmap;
int bkl_locked;
spinlock_t inkernel_now;
int inkernel_lock;
struct Thread *bkl_owner;
thread_list_t bkl_blocked_list;

/*
 * Interrupt handling
 */

int irq_mask_cnt[NIRQ];
int irq_handler_cnt[NIRQ];
isr_handler_list_t isr_handler_list[NIRQ];

int max_isr_handler;
struct ISRHandler *isr_handler_table;
isr_handler_list_t isr_handler_free_list;


/*
 * Timer
 */
struct Thread *timer_thread;
timer_list_t timing_wheel[JIFFIES_PER_SECOND];
struct Rendez timer_rendez;

volatile spinlock_t timer_slock;
volatile long hardclock_time;
volatile long softclock_time;

uint64_t usage_start_usec = 0;

// timer_list_t free_timer_list;
// int max_timer;
// struct Timer *timer_table;

