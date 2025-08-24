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
 * Global variables related to memory management.
 */

#include <kernel/types.h>
#include <kernel/filesystem.h>
#include <kernel/proc.h>
#include <kernel/kqueue.h>
#include <kernel/interrupt.h>
#include <kernel/msg.h>


/*
 * Memory Management
 */
vm_size mem_size;
int max_page;
struct Page *page_table;

int free_page_cnt;
int dirty_page_cnt;
int busy_page_cnt;

//bool dirty_queues_busy;
//struct Rendez dirty_queues_rendez;

//struct RWLock cache_lock;

page_list_t free_page_queue;        // Pages that are clean and can be reused by other vnodes

page_list_t page_lookup_hash[PAGE_LOOKUP_HASH_SZ];

struct Rendez page_list_rendez;


int max_memregion;
struct MemRegion *memregion_table;
memregion_list_t unused_memregion_list;


