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
 * track of page table entries referencing a Pageframe.
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


/*
 *
 */
static uint32_t pmap_calc_pa_bits(bits32_t flags)
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

  if ((flags & MEM_MASK) == MEM_ALLOC) {
    if ((flags & CACHE_MASK) == CACHE_DEFAULT)
      pa_bits |= L2_C; // FIXME: | L2_S | L2_B;  
    else if ((flags & CACHE_MASK) == CACHE_WRITEBACK)
      pa_bits |= 0; // L2_B | L2_C;
    else if ((flags & CACHE_MASK) == CACHE_WRITETHRU)
      pa_bits |= 0; // L2_C;
    else if ((flags & CACHE_MASK) == CACHE_WRITECOMBINE)
      pa_bits |= 0; // L2_B;
    else if ((flags & CACHE_MASK) == CACHE_UNCACHEABLE)
      pa_bits |= 0;
    else
      pa_bits |= L2_C; // | L2_S;// | L2_B;
  }

  if ((flags & MEM_MASK) == MEM_PHYS) {
    if ((flags & CACHE_MASK) == CACHE_DEFAULT)
      pa_bits |= 0;
    else if ((flags & CACHE_MASK) == CACHE_WRITEBACK)
      pa_bits |= 0; // L2_B | L2_C;
    else if ((flags & CACHE_MASK) == CACHE_WRITETHRU)
      pa_bits |= 0; // L2_C;
    else if ((flags & CACHE_MASK) == CACHE_WRITECOMBINE)
      pa_bits |= 0; // L2_B;
    else if ((flags & CACHE_MASK) == CACHE_UNCACHEABLE)
      pa_bits |= 0;
    else
      pa_bits |= 0;
  }

  return pa_bits;
}


void pmap_write(uint32_t *addr, uint32_t data)
{
  hal_mmio_write(addr, data);
  hal_flush_dcache(addr, addr + 1);
  hal_dsb();
  hal_invalidate_tlb();
//  hal_invalidate_tlb_va(data & 0xFFFFF000);  // This should still work for L1
  hal_invalidate_icache();
  hal_invalidate_branch();
  hal_dsb();
  hal_isb();
}


/*
 * 4k page tables,  1k real PTEs,  3k virtual-page linked list and flags (packed
 * 12 bytes)
 */
int pmap_enter(struct AddressSpace *as, vm_addr va, vm_addr pa, bits32_t flags)
{
  struct Pmap *pmap;
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  uint32_t pa_bits;
  struct Pageframe *pf;
  struct Pageframe *ptpf;
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
    
    // pmap->l1_table[pde_idx] = (uint32_t)phys_pt | L1_TYPE_C;    
    pmap_write(&pmap->l1_table[pde_idx], (uint32_t)phys_pt | L1_TYPE_C);
  } else {
    phys_pt = (uint32_t *)(pmap->l1_table[pde_idx] & L1_C_ADDR_MASK);
    pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);
  }

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;

  // FIXME: Doesn't this delete pagetable if single PTE entry is not already free?
  if ((pt[pte_idx] & L2_TYPE_MASK) != L2_TYPE_INV) {
    pmap_free_pagetable(pt);
    pmap->l1_table[pde_idx] = L1_TYPE_INV;
    return -EINVAL;
  }

  if ((flags & MEM_MASK) != MEM_PHYS) {
    pf = &pageframe_table[pa / PAGE_SIZE];
    LIST_ADD_HEAD(&pf->pmap_pageframe.vpte_list, vpte, link);
  }
  
  vpte->flags = flags;
  
	pmap_write(&pt[pte_idx], pa | pa_bits);

  ptpf = pmap_va_to_pf((vm_addr)pt);
  ptpf->reference_cnt++;

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
  struct Pageframe *pf;
  struct Pageframe *ptpf;
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

  if ((vpte->flags & MEM_PHYS) == 0) {
    pf = pmap_pa_to_pf(current_paddr);
    LIST_REM_ENTRY(&pf->pmap_pageframe.vpte_list, vpte, link);
  }

  vpte->flags = 0;
  
  pmap_write(&pt[pte_idx], L2_TYPE_INV);

  ptpf = pmap_va_to_pf((vm_addr)pt);
  ptpf->reference_cnt--;

  if (ptpf->reference_cnt == 0) {
    pmap_free_pagetable(pt);    
    pmap_write(&pmap->l1_table[pde_idx], L1_TYPE_INV);
  }

  return 0;
}


/*
 * Change protections on a page
 */
int pmap_protect(struct AddressSpace *as, vm_addr va, bits32_t flags)
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

	pmap_write(&pt[pte_idx], pa | pa_bits);

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

  *pa = current_paddr;
  *flags = vpte->flags;
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
  struct Pageframe *pf;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  if ((pf = alloc_pageframe(VPAGETABLE_SZ)) == NULL) {
    return NULL;
  }
  
  pt = (uint32_t *)pmap_pf_to_va(pf);
  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);

  for (t = 0; t < 256; t++) {
    *(pt + t) = L2_TYPE_INV;

    vpte = vpte_base + t;
    vpte->link.next = NULL;
    vpte->link.prev = NULL;
    vpte->flags = 0;
  }

  return pt;
}


/*
 *
 */
void pmap_pageframe_init(struct PmapPageframe *ppf)
{
  LIST_INIT(&ppf->vpte_list);
}


/* @brief   Free a pagetable
 *
 * @param   pt, virtual address of pagetable in the kernel
 */
void pmap_free_pagetable(uint32_t *pt)
{
  struct Pageframe *pf;
  pf = pmap_va_to_pf((vm_addr)pt);

  free_pageframe(pf);
}


/*
 *
 */
int pmap_create(struct AddressSpace *as)
{
  uint32_t *pd;
  int t;
  struct Pageframe *pf;

	
  if ((pf = alloc_pageframe(PAGEDIR_SZ)) == NULL) {
    Error("PmapCreate failed to alloc pageframe");
    return -1;
  }

  pd = (uint32_t *)pmap_pf_to_va(pf);

  for (t = 0; t < 2048; t++) {
    *(pd + t) = L1_TYPE_INV;
  }

  for (t = 2048; t < 4096; t++) {
    *(pd + t) = root_pagedir[t];
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
  struct Pageframe *pf;

  pd = as->pmap.l1_table;
  pf = pmap_va_to_pf((vm_addr)pd);
  free_pageframe(pf);
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
vm_addr pmap_pf_to_pa(struct Pageframe *pf)
{
  return (vm_addr)pf->physical_addr;
}


/*
 *
 */
struct Pageframe *pmap_pa_to_pf(vm_addr pa)
{
  return &pageframe_table[(vm_addr)pa / PAGE_SIZE];
}


/*
 *
 */
vm_addr pmap_pf_to_va(struct Pageframe *pf)
{
  return pmap_pa_to_va((pf->physical_addr));
}


/*
 *
 */
struct Pageframe *pmap_va_to_pf(vm_addr va)
{
  return &pageframe_table[pmap_va_to_pa(va) / PAGE_SIZE];
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
  /* Assign new user's page dir to TTBR0 register */
  uint32_t va, pa, flags;

  hal_dsb();
  hal_isb();

  hal_set_ttbr0((pmap_va_to_pa((vm_addr)next->as.pmap.l1_table)));

  hal_isb();
  hal_invalidate_tlb();
  hal_invalidate_branch();
  hal_invalidate_icache();  

  hal_dsb();
  hal_isb();
}


/* @brief   Set a page table entry in the kernel's VFS cache area of memory
 *
 * @param   addr,
 * @param   paddr,
 * @return  0 on success, negative errno on failure
 *
 * 4k page tables,  1k real PTEs,  3k virtual-page linked list and flags (packed
 * 12 bytes)
 */
int pmap_cache_enter(vm_addr addr, vm_addr paddr)
{
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  uint32_t pa_bits;
  struct Pageframe *pf;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  pa_bits = L2_TYPE_S;
  pa_bits |= L2_AP_RWK;   // read/write kernel-only
  // pa_bits |= L2_NX;
  pa_bits |= L2_C;  // FIXME: Add  L2_B to pa_bits

  pde_idx = (addr & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  phys_pt = (uint32_t *)(root_pagedir[pde_idx] & L1_C_ADDR_MASK);

  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (addr & L2_ADDR_BITS) >> L2_IDX_SHIFT;

  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;
  pf = &pageframe_table[paddr / PAGE_SIZE];
  vpte->flags = PROT_READ | PROT_WRITE;
  LIST_ADD_HEAD(&pf->pmap_pageframe.vpte_list, vpte, link);
  // TODO: Increment/Decrement pf reference cnt or not?

  pt[pte_idx] = paddr | pa_bits;

	hal_dsb();
	hal_invalidate_tlb_va(addr & 0xFFFFF000);
  hal_invalidate_branch();
  hal_invalidate_icache();
  hal_dsb();
  hal_isb();

  return 0;
}


/* @brief   Unmaps a segment from the address space pointed to by pmap.
 * @param   va,
 * @return  0 on success, negative errno on failure
 */
int pmap_cache_remove(vm_addr va)
{
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;
  vm_addr current_paddr;
  struct Pageframe *pf;
  struct PmapVPTE *vpte;
  struct PmapVPTE *vpte_base;

  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  phys_pt = (uint32_t *)(root_pagedir[pde_idx] & L1_C_ADDR_MASK);
  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  current_paddr = pt[pte_idx] & L2_ADDR_MASK;

  vpte_base = (struct PmapVPTE *)((uint8_t *)pt + VPTE_TABLE_OFFS);
  vpte = vpte_base + pte_idx;

  pf = pmap_pa_to_pf(current_paddr);
  LIST_REM_ENTRY(&pf->pmap_pageframe.vpte_list, vpte, link);

  vpte->flags = 0;
  pt[pte_idx] = L2_TYPE_INV;

  // TODO: Increment/Decrement pf reference cnt or not?
	hal_dsb();
	hal_invalidate_tlb_va(va & 0xFFFFF000);
  hal_invalidate_branch();
  hal_invalidate_icache();
  hal_dsb();
  hal_isb();

  return 0;
}


/* @brief   Get the physcial address of a page in the file cache
 * 
 * @param   va,
 * @param   pa,
 * @return  0 on success, negative errno on failure
 *
 * NOTE: This assumes the page is present, no checks are performed.
 */
int pmap_cache_extract(vm_addr va, vm_addr *pa)
{
  uint32_t *pt, *phys_pt;
  int pde_idx, pte_idx;

  pde_idx = (va & L1_ADDR_BITS) >> L1_IDX_SHIFT;

  phys_pt = (uint32_t *)(root_pagedir[pde_idx] & L1_C_ADDR_MASK);
  pt = (uint32_t *)pmap_pa_to_va((vm_addr)phys_pt);

  pte_idx = (va & L2_ADDR_BITS) >> L2_IDX_SHIFT;
  *pa = pt[pte_idx] & L2_ADDR_MASK;
  return 0;
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
  
  Info("pmap_pagetable_walk(as:%08x, access:%08x, va:%08x", (uint32_t)as, access, (uint32_t)vaddr);
  
  bvaddr = (vm_addr)vaddr & ~(PAGE_SIZE - 1);
  page_offset = (vm_addr)vaddr & (PAGE_SIZE -1);
      
  if (pmap_extract(as, bvaddr, &bpaddr, &flags) != 0) {
    Info("Cannot extract pte (note:we don't lazy alloc pages)");
    
    return -EFAULT;
  } else {    
    if (access & PROT_WRITE) {
      if (flags & PROT_WRITE) {
        if (flags & MAP_COW) {
          fault = true;
        }
      } else {      
        Info("pmap_pagetable_walk -EFAULT write on non-write page");
        return -EFAULT;
      }
    }
  }
    
  if (fault) {
    if (page_fault(bvaddr, access) != 0) {
      Info("pmap_pagetable_walk -EFAULT 2");
      return -EFAULT;
    }
    
    if (pmap_extract(as, (vm_addr)bvaddr, &bpaddr, &flags) != 0) {
        Info("pmap_pagetable_walk -EFAULT 3");
      return -EFAULT;
    }
  }    

  *rkaddr = (void *)pmap_pa_to_va(bpaddr + page_offset);

  Info("0 = pmap_pagetable_walk() rkaddr:%08x", (uint32_t)*rkaddr);

  return 0;
}

