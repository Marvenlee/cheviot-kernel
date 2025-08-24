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

/*
 * ARM-Specific memory management code.  Deals with converting high-level
 * VirtSeg and PhysSeg segment tables into CPU page tables.
 *
 * The CPU's page directories are 16k and the page tables are 1k.  We use 4K
 * page size and so are able to fit 1K of page tables plus a further 12 bytes
 * per page table entry for flags and linked list next/prev pointers to keep
 * track of page table entries referencing a Page.
 */

//#define KDEBUG

#include <kernel/board/arm.h>
#include <kernel/board/globals.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <sys/mman.h>

/*
 *
 */
static uint32_t pmap_calc_pa_bits(int flags)
{
  uint32_t pa_bits;

  pa_bits = L2_TYPE_S;

  if ((flags & PROT_WRITE) && !(flags & MAP_COW)) {
    pa_bits |= L2_AP_RWKU;    // read/write kernel & user
  } else {
    pa_bits |= L2_AP_RKU;     // read-only kernel & user
  }

  if ((flags & PROT_EXEC) != PROT_EXEC) {
    pa_bits |= L2_NX;
  }

  if ((flags & MAP_PHYS) == 0) {
    if ((flags & VM_CACHE_MASK) == CACHE_DEFAULT)
      pa_bits |= L2_C | L2_B; // FIXME: | L2_S | L2_B;  
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITEBACK)
      pa_bits |= L2_B | L2_C;
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITETHRU)
      pa_bits |= L2_C;
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITECOMBINE)
      pa_bits |= L2_B;
    else if ((flags & VM_CACHE_MASK) == CACHE_UNCACHEABLE)
      pa_bits |= 0;
    else
      pa_bits |= 0;
  }

  if ((flags & MAP_PHYS) == MAP_PHYS) {
    if ((flags & VM_CACHE_MASK) == CACHE_DEFAULT)
      pa_bits |= 0;
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITEBACK)
      pa_bits |= L2_B | L2_C;
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITETHRU)
      pa_bits |= L2_C;
    else if ((flags & VM_CACHE_MASK) == CACHE_WRITECOMBINE)
      pa_bits |= L2_B;
    else if ((flags & VM_CACHE_MASK) == CACHE_UNCACHEABLE)
      pa_bits |= 0;
    else
      pa_bits |= 0;
  }

  return pa_bits;
}


void pmap_write_l1(uint32_t *pd, int i, uint32_t data)
{
  uint32_t *pde;
  
  pde = &pd[i];

  hal_dsb();
  *(volatile uint32_t *)pde = data;
  hal_dsb();
  hal_dmb();
  
  hal_flush_dcache((uint32_t *)pde, (uint8_t *)pde + sizeof(uint32_t));

  hal_invalidate_tlb();
  hal_invalidate_icache();
  hal_invalidate_branch();
}


void pmap_write_l2(uint32_t *pt, int i, uint32_t data)
{
  uint32_t *pte;
  
  pte = &pt[i];

  hal_dsb();
  *(volatile uint32_t *)pte = data;
  hal_dsb();
  hal_dmb();

  hal_flush_dcache((uint32_t *)pte, (uint8_t *)pte + sizeof(uint32_t));

  hal_invalidate_tlb();
  hal_invalidate_icache();
  hal_invalidate_branch();
}


/*
 * 4k page tables,  1k real PTEs,  3k virtual-page linked list and flags (packed
 * 12 bytes)
 */
int pmap_enter(struct AddressSpace *as, vm_addr va, vm_addr pa, int flags)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  uint32_t pa_bits;
  struct Page *page;
  struct Page *ptpage;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  if (va == 0) {
    return -EFAULT;
  }

  pa_bits = pmap_calc_pa_bits(flags);
  pmap = &as->pmap;
  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;
  

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
    if ((pt = pmap_alloc_pagetable()) == NULL) {
      return -ENOMEM;
    }

    phys_pt = (uint32_t *)pmap_va_to_pa((vm_addr)pt);
    pmap_write_l1(pmap->l1_table, pde_idx, (uint32_t)phys_pt | L1_TYPE_C);
  } else {
    phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
    pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);
  }

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;

  // FIXME: Doesn't this delete pagetable if single PTE entry is not already free?
  if ((pt[pte_idx] & L2_TYPE_MASK) != L2_TYPE_INV) {
  	pmap_write_l1(pmap->l1_table, pde_idx, L1_TYPE_INV);
    pmap_free_pagetable(pt);
    return -EINVAL;
  }

  if ((flags & MAP_PHYS) == 0) {
    page = &page_table[pa / PAGE_SIZE];
    LIST_ADD_HEAD(&page->pmap_page.vpte_list, vpte, link);
  }
  
  vpte->flags = flags;
  

  ptpage = pmap_va_to_page((vm_addr)pt);
  ptpage->reference_cnt++;

	pmap_write_l2(pt, pte_idx, pa | pa_bits);
  hal_invalidate_tlb_va(va);

  return 0;
}


/*
 * Unmaps a segment from the address space pointed to by pmap.
 */
int pmap_remove(struct AddressSpace *as, vm_addr va)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  vm_addr current_paddr;
  struct Page *page;
  struct Page *ptpage;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  // TODO : Check user base user ceiling

  if (va == 0) {
    return -EFAULT;
  }

  pmap = &as->pmap;
  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
    return -EINVAL;
  }

  phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  current_paddr = pt[pte_idx] & L2_ADDR_MASK;

  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;

  // Unmap any existing mapping, then map either anon or phys mem
  if ((pt[pte_idx] & L2_TYPE_MASK) == L2_TYPE_INV) {
    return -EINVAL;
  }

  if ((vpte->flags & MAP_PHYS) == 0) {
    page = pmap_pa_to_page(current_paddr);
    LIST_REM_ENTRY(&page->pmap_page.vpte_list, vpte, link);
  }

  vpte->flags = 0;
  
  pmap_write_l2(pt, pte_idx, L2_TYPE_INV);
  hal_invalidate_tlb_va(va);

  ptpage = pmap_va_to_page((vm_addr)pt);
  ptpage->reference_cnt--;

  if (ptpage->reference_cnt == 0) {
    pmap_write_l1(pmap->l1_table, pde_idx, L1_TYPE_INV);
    pmap_free_pagetable(pt);    
  }

  
  return 0;
}


/*
 * Change protections on a page
 */
int pmap_protect(struct AddressSpace *as, vm_addr va, int flags)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;
  uint32_t pa_bits;
  vm_addr pa;

  pmap = &as->pmap;

  if (va == 0) {
    return 0;
  }

  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
    Error("******** pmap_protect failed ******");
  	KernelPanic();
  }

  phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;
  pa = pt[pte_idx] & L2_ADDR_MASK;

  if ((pt[pte_idx] & L2_TYPE_MASK) == L2_TYPE_INV) {
    Error("******** pmap_protect failed ******");
    KernelPanic();
  }

  vpte->flags = flags;
  pa_bits = pmap_calc_pa_bits(vpte->flags);

	pmap_write_l2(pt, pte_idx, pa | pa_bits);
  hal_invalidate_tlb_va(va);
  
  return 0;
}


/*
 * Extract the physical address and flags associates with a virtual address
 */
int pmap_extract(struct AddressSpace *as, vm_addr va, vm_addr *pa, uint32_t *flags)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  vm_addr current_paddr;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  Info("pmap_extract(as:%08x, va:%08x", (uint32_t)as, (uint32_t)va);

  pmap = &as->pmap;
  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
    Info("extract failed, no page table");
    return -1;    
  }

  Info("pde_idx %d: pde:%08x", pde_idx, pmap->l1_table[pde_idx]);

  phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;
  current_paddr = pt[pte_idx] & L2_ADDR_MASK;

  if ((pt[pte_idx] & L2_TYPE_MASK) == L2_TYPE_INV) {
    Info("extract failed, no page table entry");
    return -1;
  }

  Info("pmap_extract: va:%08x, pte:%08x", (uint32_t)va, (uint32_t)pt[pte_idx]);

  if (pa != NULL) {
    *pa = current_paddr;
  }
  
  if (flags != NULL) {
    *flags = vpte->flags;
  }
  
  return 0;
}


/*
 *
 */
bool pmap_is_pagetable_present(struct AddressSpace *as, vm_addr addr)
{
  struct Pmap *pmap;
  int pde_idx;

  pmap = &as->pmap;
  pde_idx = (addr & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
    return false;
  } else {
    return true;
  }
}


/*
 *
 */
bool pmap_is_page_present(struct AddressSpace *as, vm_addr addr)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;

  pmap = &as->pmap;
  pde_idx = (addr & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  if ((pmap->l1_table[pde_idx] & L1_TYPE_MASK) == L1_TYPE_INV) {
		if (addr >= 0x0001C000 && addr <= 0x00028000) {
	  	Error("page table not present addr:%08x, pde_idx=%d", addr, pde_idx);
		}
  	
    return false;
  } else {
    phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
    pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

    pte_idx = (addr & L2_ADDR_BITS) >> L2_IDX_SHIFT;

    if ((pt[pte_idx] & L2_TYPE_MASK) != L2_TYPE_INV) {
      return true;
    } else {
      return false;
    }
  }
}


/* @brief   Allocate memory for a pagetable
 * 
 * @return  Virtual address of the pagetable in the kernel or NULL on error
 */
uint32_t *pmap_alloc_pagetable(void)
{
  int t;
  uint32_t *pt;
  struct Page *page;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  if ((page = alloc_page()) == NULL) {
    return NULL;
  }
  
  pt = (uint32_t *)pmap_page_to_va(page);
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);

  for (t = 0; t < 256; t++) {
    pmap_write_l2 (pt, t, L2_TYPE_INV);

    vpte = vpte_base + t;
    vpte->link.next = NULL;
    vpte->link.prev = NULL;
    vpte->flags = 0;
  }

  hal_flush_dcache((void *)pt, (void *)pt + PAGE_SIZE);

  return pt;
}


/*
 *
 */
void pmap_page_init(struct PmapPage *ppage)
{
  LIST_INIT(&ppage->vpte_list);
}


/* @brief   Free a pagetable
 *
 * @param   pt, virtual address of pagetable in the kernel
 */
void pmap_free_pagetable(uint32_t *pt)
{
  struct Page *ptpage;
  ptpage = pmap_va_to_page((vm_addr)pt);

  free_page(ptpage);
}


/*
 *
 */
int pmap_create(struct AddressSpace *as)
{
  struct PmapPagedir *ppd;
  uint32_t *pd = NULL;
  int t;

  Info("pmap_create: as:%08x", (uint32_t)as);
	
	ppd = LIST_HEAD(&free_pmappagedir_list);
	
	if (ppd == NULL) {
	  return -1;
	}

  LIST_REM_HEAD(&free_pmappagedir_list, free_link);
  
  pd = ppd->pagedir;

  Info(".. pagedir:%08x", (uint32_t)pd);

  for (t = 0; t < 2048; t++) {
    pmap_write_l1(pd, t, L1_TYPE_INV);
  }

  for (t = 2048; t < 4096; t++) {
    pmap_write_l1(pd, t, root_pagedir[t]);
  }

  as->pmap.l1_table = pd;

  return 0;
}


/*
 *
 */
void pmap_destroy(struct AddressSpace *as)
{
  uint32_t *pd;
  int index;
  
  pd = as->pmap.l1_table;

  index = (pd - pagedir_table) / 4096; 
  
  LIST_ADD_TAIL(&free_pmappagedir_list, &pmappagedir_table[index], free_link);
}


/*
 * Checks if the flags argument of VirtualAlloc() and VirtualAllocPhys() is
 * valid and supported.  Returns 0 on success, -1 otherwise.
 */
int pmap_supports_cache_policy(bits32_t flags)
{
  return 0;
}


/*
 *
 */
vm_addr pmap_page_to_pa(struct Page *page)
{
  return (vm_addr)page->physical_addr;
}


/*
 *
 */
struct Page *pmap_pa_to_page(vm_addr pa)
{
  return &page_table[(vm_addr)pa / PAGE_SIZE];
}


/*
 *
 */
vm_addr pmap_page_to_va(struct Page *page)
{
  return pmap_pa_to_va((page->physical_addr));
}


/*
 *
 */
struct Page *pmap_va_to_page(vm_addr va)
{
  return &page_table[pmap_va_to_pa(va) / PAGE_SIZE];
}


/*
 *
 */
vm_addr pmap_va_to_pa(vm_addr vaddr)
{
  return vaddr - 0x80000000;
}


/*
 *
 */
vm_addr pmap_pa_to_va(vm_addr paddr)
{
  return paddr + 0x80000000;
}


/*
 * Flushes the CPU's TLBs once page tables are updated.
 */
void pmap_flush_tlbs(void)
{
  hal_dsb();
  hal_isb();
	hal_invalidate_tlb();
  hal_invalidate_branch();
  
	hal_dsb();
}

/*
 * Switches the address space from the current process to the next process.
 * FIXME: Need to update code, probably don't need to flush caches. 
 */
void pmap_switch(struct Process *next, struct Process *current)
{
  uint32_t pagedir = (uint32_t)(next->as.pmap.l1_table);
  
  hal_dsb();
  hal_isb();

  hal_set_ttbr0(pmap_va_to_pa(pagedir));

  hal_isb();
  hal_invalidate_tlb();
  
  hal_invalidate_branch();
  hal_invalidate_icache();
}


/*
 *
 */
int pmap_pagetable_walk(struct AddressSpace *as, uint32_t access, void *vaddr, void **rkaddr)
{
  vm_addr bvaddr;
  vm_addr bpaddr;
  uint32_t flags;
  bool fault = false;
  uint32_t page_offset;
  
  bvaddr = ALIGN_DOWN((vm_addr)vaddr, PAGE_SIZE);
  page_offset = (vm_addr)vaddr % PAGE_SIZE;
  
  if (pmap_extract(as, bvaddr, &bpaddr, &flags) == 0) {
    if (access & PROT_WRITE) {
      if (flags & PROT_WRITE) {
        if (flags & MAP_COW) {
          fault = true;
        }
      } else {      
        Warn("pmap_pagetable_walk -EFAULT write on non-write page");
        return -EFAULT;
      }
    }
  } else {
    Warn("Cannot extract pte (note:we don't lazy alloc pages)");    
    return -EFAULT;
  }
    
  if (fault) {
    if (page_fault(bvaddr, access) != 0) {
      Warn("pmap_pagetable_walk -EFAULT 2");
      return -EFAULT;
    }
    
    if (pmap_extract(as, (vm_addr)bvaddr, &bpaddr, &flags) != 0) {
        Warn("pmap_pagetable_walk -EFAULT 3");
      return -EFAULT;
    }
  }    

  *rkaddr = (void *)pmap_pa_to_va(bpaddr + page_offset);
  return 0;
}

