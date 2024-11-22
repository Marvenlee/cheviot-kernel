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

#ifndef MACHINE_BOARD_RASPBERRY_PI_4_INIT_H
#define MACHINE_BOARD_RASPBERRY_PI_4_INIT_H

#include <kernel/types.h>


// FIXME: Move into boot.h  (or bootinfo.h)
#define BOOT_BASE_ADDR      0x00001000
#define BOOT_CEILING_ADDR   0x00010000


// Externs
extern uint8_t _stext;
extern uint8_t _ebss;
extern vm_addr _heap_base;
extern vm_addr _heap_current;

extern vm_addr core_pagetable_base;
extern vm_addr core_pagetable_ceiling;

// FIXME: statically allocate only for CPU 0, need stacks allocated for other CPUs
extern int svc_stack_top;
extern int interrupt_stack_top;
extern int exception_stack_top;


/*
 * Prototypes
 */

// arm/debug.c
void InitDebug(void);
void InitProcesses(void);

void BootPrint(char *s, ...);
void BootPanic(char *s);

// arm/init_arm.c
void init_arm(void);
void init_timer_registers(void);

// arm/init_proc.c
void init_processes(void);
void start_scheduler(void);
void init_cpu_tables(void);


// arm/init_vm.c
void init_vm(void);
void init_memregion_list(void);
void init_io_pagetables(void);
void init_buffer_cache_pagetables(void);
void init_memory_map(void);
void init_pmappagedir_table(void);
void init_pageframe_flags(vm_addr base, vm_addr ceiling, bits32_t flags);
void coalesce_free_pageframes(void);
void *io_map(vm_addr pa, size_t sz, bool bufferable);
void init_io_addresses(void);

// arm/main.c
void init_bootstrap_allocator(void);
void *bootstrap_alloc(vm_size size);


#endif
