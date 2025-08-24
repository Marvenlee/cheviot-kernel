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

#define KDEBUG

#include <kernel/arch.h>
#include <kernel/board/peripheral_base.h>
#include <kernel/board/boot.h>
#include <kernel/board/globals.h>
#include <kernel/board/init.h>
#include <kernel/board/aux_uart.h>
#include <kernel/board/interrupt.h>
#include <kernel/board/timer.h>
#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/lists.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <machine/cheviot_hal.h>


/* @brief   Set pointers to RPI4 peripherals
 *
 * The following RPI4 peripherals are mapped along with the kernel into the root_pagedir
 * page tables.  See bootstrap.c in the bootloader.
 */
void init_io_addresses(void)
{
  timer_regs = (struct bcm2711_timer_registers *)bootinfo->timer_base;
  aux_regs   = (struct bcm2711_aux_registers *)bootinfo->aux_base;  	
  gic_dist_regs      = (struct bcm2711_gic_dist_registers *) bootinfo->gicd_base;
  gic_cpu_iface_regs = (struct bcm2711_gic_cpu_iface_registers *) bootinfo->gicc_base[0];
}


/* @brief   Initialize the virual memory management system.
 *
 * Note that all of physical memory is mapped into the kernel.  Areas for the
 * timer, aux uart and interrupt controller are already mapped into the kernel by the
 * bootloader.
 */
void init_vm(void)
{
  vm_addr pa;

  Info("init_vm()...");
  
  root_pagedir = bootinfo->root_pagedir;

  LIST_INIT(&free_page_queue);
//  LIST_INIT(&clean_page_queue);

//  for (int t = 0; t < DIRTY_HASH; t++) {
//    LIST_INIT(&dirty_page_queue[t]);
//  }

  for (int t = 0; t < PAGE_LOOKUP_HASH_SZ; t++) {
    LIST_INIT(&page_lookup_hash[t]);
  }

  init_memory_map();

  init_memregion_list();

  init_pmappagedir_table();

  boot_base = BOOT_BASE_ADDR;
  boot_ceiling = BOOT_CEILING_ADDR;
  core_pagetable_base = bootinfo->pagetable_base;
  core_pagetable_ceiling = bootinfo->pagetable_ceiling;

  // 0 to boot base, is this first 4k ?
  init_pageframe_flags((vm_addr)0, (vm_addr)boot_base, PGF_KERNEL | PGF_INUSE);

  // boot_base to boot ceiling.  bootsector 0k to 64k ?
  init_pageframe_flags(boot_base, boot_ceiling, PGF_KERNEL | PGF_INUSE);
  
  // Reserve memory for videocore      
  init_pageframe_flags(bootinfo->videocore_base, bootinfo->videocore_ceiling, PGF_INUSE);
        
  // Reserve kernel pages from start of text 0x80010000 (2MB+64k) to ceiling of kernel's bss.      
  // This is from 64k phys to physical address of end of ebss.
  init_pageframe_flags(VirtToPhys((vm_addr)&_stext), VirtToPhys((vm_addr)&_ebss), PGF_KERNEL | PGF_INUSE);

  // Core page tables allocated above kernel > 1MB, below heap which is allocated after 
  init_pageframe_flags(VirtToPhys(core_pagetable_base), VirtToPhys(core_pagetable_ceiling), PGF_KERNEL | PGF_INUSE);
  
  
  // FIXME: Should it be any pages not marked as PGF_INUSE  (otherwise we could be overwriting the videocore)
  // if mapped in middle of physical mem.
    
  // Reserve pages for kernel heap
  init_pageframe_flags(VirtToPhys(_heap_base), VirtToPhys(_heap_current), PGF_KERNEL | PGF_INUSE);  

  // Mark pages from top of kernel's heap to ifs_exe_base as free
//  init_pageframe_flags(VirtToPhys(_heap_current), (vm_addr)bootinfo->ifs_exe_base, 0);

  init_free_page_list(VirtToPhys(_heap_current), (vm_addr)bootinfo->ifs_exe_base);

  // Mark IFS image pages as in-use. This was moved to top of physical memory.
  // This will be mapped into the initial root IFS process.
  
  init_pageframe_flags(bootinfo->ifs_image, bootinfo->ifs_image + bootinfo->ifs_image_size, PGF_INUSE);

//  init_pageframe_flags(bootinfo->ifs_image, (vm_addr)bootinfo->mem_size, PGF_INUSE);

  Info ("reserved from 0 to boot base");
  
  Info ("mem_size : %08x", (uint32_t)bootinfo->mem_size);
  
  Info ("reserved boot base : %08x", boot_base);
  Info ("reserved boot ceil : %08x", boot_ceiling);
  Info ("reserved _stext : %08x", VirtToPhys((uint32_t)&_stext));
  Info ("reserved _ebss  : %08x", VirtToPhys((uint32_t)&_ebss));
  Info ("reserved core pt base : %08x", VirtToPhys(core_pagetable_base));
  Info ("reserved core pt ceil : %08x", VirtToPhys(core_pagetable_ceiling));
  Info ("reserved kernel heap base : %08x", VirtToPhys(_heap_base));
  Info ("reserved kernel heap ceil : %08x", VirtToPhys(_heap_current));


  Info ("reserved videocore base : %08x", bootinfo->videocore_base);
  Info ("reserved videocore ceil : %08x", bootinfo->videocore_base);
  Info ("reserved IFS base : %08x", bootinfo->ifs_image);
  Info ("reserved IFS ceil : %08x", (vm_addr)bootinfo->mem_size);

  // Error checking, all pages should have 0 or 1 references.
  for (pa = 0; pa < (vm_addr)mem_size; pa += PAGE_SIZE) {
    KASSERT(page_table[pa / PAGE_SIZE].reference_cnt <= 1);
  }

  Info("... init_vm() done");
}


/* @brief   Initialise the pageframes in the memory map
 */
void init_memory_map(void)
{
  for (int t = 0; t < max_page; t++) {
    InitRendez(&page_table[t].rendez);

    page_table[t].size = PAGE_SIZE;
    page_table[t].physical_addr = t * PAGE_SIZE;
    page_table[t].vaddr = (void *)pmap_pa_to_va(page_table[t].physical_addr);

    page_table[t].mflags = 0;
    
    page_table[t].vnode = NULL;
    page_table[t].file_offset = 0;

    page_table[t].bflags = 0;

//    page_table[t].expiration_ticks = 0;
    
    page_table[t].reference_cnt = 0;

    pmap_page_init(&page_table[t].pmap_page);
  }
}


/*
 *
 */
void init_memregion_list(void)
{
  LIST_INIT(&unused_memregion_list);

  for (int t = 0; t < max_memregion; t++) {
    memregion_table[t].type = MR_TYPE_UNALLOCATED;
    LIST_ADD_TAIL(&unused_memregion_list, &memregion_table[t], unused_link);
  }
}


/* @brief   Initialize table for allocating 16KB ARM Page Directories
 *
 */ 
void init_pmappagedir_table(void)
{
  LIST_INIT(&free_pmappagedir_list);
  
  for (int t=0; t<max_process; t++) {
    LIST_ADD_TAIL(&free_pmappagedir_list, &pmappagedir_table[t], free_link);
    pmappagedir_table[t].pagedir = &pagedir_table[t * N_PAGEDIR_PDE];
  }
} 


/*
 *
 */
void init_pageframe_flags(vm_addr base, vm_addr ceiling, bits32_t flags)
{
  vm_addr pa;

  base = ALIGN_DOWN(base, PAGE_SIZE);
  ceiling = ALIGN_UP(ceiling, PAGE_SIZE);

  for (pa = base; pa < ceiling; pa += PAGE_SIZE) {
    page_table[pa / PAGE_SIZE].mflags = flags;
  }
}


/*
 * FIXME: Ignore params
 */
void init_free_page_list(vm_addr base, vm_addr ceiling)
{
  vm_addr pa;

  base = 0;
  ceiling = (vm_addr)bootinfo->ifs_image;
  
  for (pa = base; pa < ceiling; pa += PAGE_SIZE) {
    if (pa > VirtToPhys(_heap_current) && (page_table[pa / PAGE_SIZE].mflags & PGF_INUSE) == 0) {
  #if 0
      LIST_ADD_HEAD(&free_page_queue, &page_table[pa / PAGE_SIZE], free_link);
  #else
      LIST_ADD_TAIL(&free_page_queue, &page_table[pa / PAGE_SIZE], free_link);
  #endif
      page_table[pa / PAGE_SIZE].bflags = 0;
    }
  }
}


