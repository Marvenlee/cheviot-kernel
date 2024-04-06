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

#ifndef MACHINE_BOARD_RASPBERRY_PI_1_GLOBALS_H
#define MACHINE_BOARD_RASPBERRY_PI_1_GLOBALS_H

#include <kernel/board/arm.h>
#include <kernel/board/boot.h>
#include <kernel/board/task.h>
#include <kernel/filesystem.h>
#include <kernel/lists.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <kernel/interrupt.h>

/*
 * Debugger
 */

extern uint32_t screen_width;
extern uint32_t screen_height;
extern void *screen_buf;
extern uint32_t screen_pitch;

/*
 * Boot args
 */

extern struct BootInfo *bootinfo;
extern struct BootInfo bootinfo_kernel;

extern char *cfg_boot_prefix;
extern int cfg_boot_verbose;

/*
 * ARM default state of registers
 */

extern bits32_t cpsr_dnm_state;

/*
 * Interrupts
 */

extern bits32_t mask_interrupts[3];
extern bits32_t pending_interrupts[3];
extern uint32_t *vector_table;

extern struct InterruptAPI interrupt_api;

extern struct bcm2835_timer_registers *timer_regs;
extern struct bcm2835_gpio_registers *gpio_regs;
extern struct bcm2835_interrupt_registers *interrupt_regs;
extern struct bcm2835_aux_registers *aux_regs;

/*
 *
 */
extern vm_addr _heap_base;
extern vm_addr _heap_current;

extern vm_addr boot_base;
extern vm_addr boot_ceiling;

extern uint32_t *root_pagedir;
extern uint32_t *io_pagetable;
extern uint32_t *cache_pagetable;


#endif
