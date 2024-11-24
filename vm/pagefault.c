/*
 * Copyright 2014  Marven Gilhespie
 *
 * Licensed under the Apache License, segment_id 2.0 (the "License");
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
 * Page fault handling.
 */

//#define KDEBUG

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/lists.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <sys/mman.h>


/* @brief   Page fault exception handler
 */
int page_fault(vm_addr addr, bits32_t access)
{
  struct Process *current;
  uint32_t page_flags;
  vm_addr paddr;
  vm_addr src_kva;
  vm_addr dst_kva;
  struct Pageframe *pf;

  Info("page_fault(addr:%08x, access:%08x)", addr, access);
  	
  current = get_current_process();
 
  addr = ALIGN_DOWN(addr, PAGE_SIZE);
  
  if (pmap_extract(&current->as, addr, &paddr, &page_flags) != 0) {
    // Page is not present    
    // FIXME: For map_lazy we need to find segment, determine attributes and map page in
    return -1;
  }
	
	Info("extract paddr:%08x, page_flags:%08x", paddr, page_flags);

  if ((page_flags & MAP_PHYS) == MAP_PHYS) {
  	Info("fault page flags MAP_PHYS");
    return -1;
  } else if (!(access & PROT_WRITE)) {
  	Info("fault when access not writing");

    // FIXME: Make prefetch protection PROT_EXEC ??????
    // Add additional if-else test  ??????
    return -1;
  } else if ((page_flags & (PROT_WRITE | MAP_COW)) == PROT_WRITE) {
  	Info("fault page flags WRITE is not COW");

    return -1;
  } else if ((page_flags & (PROT_WRITE | MAP_COW)) != (PROT_WRITE | MAP_COW)) {
  	Info("fault page flags WRITE | COW != write|cow");

    return -1;
  }

  pf = pmap_pa_to_pf(paddr);

  KASSERT(pf->physical_addr == paddr);

  if (pf->reference_cnt > 1) {
    pf->reference_cnt--;

    if (pmap_remove(&current->as, addr) != 0) {
      Info("pmap_remove failed");
      return -1;
    }

    // Now new page frame
    if ((pf = alloc_pageframe(PAGE_SIZE)) == NULL) {
      Info("alloc_pageframe failed");
      return -1;
    }

    src_kva = pmap_pa_to_va(paddr);
    dst_kva = pmap_pa_to_va(pf->physical_addr);

    memcpy((void *)dst_kva, (void *)src_kva, PAGE_SIZE);

    page_flags = (page_flags | PROT_WRITE) & ~MAP_COW;

    if (pmap_enter(&current->as, addr, pf->physical_addr, page_flags) != 0) {
      free_pageframe(pf);
      Info("pmap_enter failed");
      return -1;
    }

    pf->reference_cnt++;
  } else if (pf->reference_cnt == 1) {
    
    // FIXME: Add pmap_modify(as, PMAP_MOD_PADDR | PMAP_MOD_FLAGS, paddr, page_flags); 
    
    if (pmap_remove(&current->as, addr) != 0) {
      Info("pmap_remove on  refcnt==1 failed");

      return -1;
    }

    page_flags = (page_flags | PROT_WRITE) & ~MAP_COW;

    if (pmap_enter(&current->as, addr, paddr, page_flags) != 0) {
      pf->reference_cnt--;
      
      
      Info("pmap_enter failed on refcnt==1");

      // TODO: Free page
      return -1;
    }

  } else {
    KernelPanic();
  }

  return 0;
}

