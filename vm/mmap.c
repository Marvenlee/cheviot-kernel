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
 * System calls for virtual memory management.
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
#include <sys/privileges.h>
#include <sys/mman.h>


/* @brief   Allocate and map an area of memory
 *
 * @param   _addr,
 * @param   len,
 * @param   prot,
 * @param   flags,
 * @param   fd,
 * @param   offset,
 * @return  Address of the mapped area or MAP_FAILED on failure
 */
void *sys_mmap(void *_addr, size_t len, int prot, int flags, int fd, off_t offset)
{
  struct Process *current;
  struct AddressSpace *as;
  vm_addr addr;
  vm_addr va, pa;
  vm_addr paddr;
  vm_addr ceiling;
  struct Pageframe *pf;
  struct MemRegion *mr;
  uint64_t privileges;
    
  Info("sys_mmap(_addr:%08x, len:%08x)", (uint32_t)_addr, (uint32_t)len);
  
  flags &= VM_FLAGS_MASK;
  prot &= VM_PROT_MASK;
  
  current = get_current_process();

  if (flags & MAP_PHYS) {
    privileges = PRIV_VALLOC | PRIV_VALLOCPHYS;
  } else {
    privileges = PRIV_VALLOC;
  }

  if (check_privileges(current, privileges) != 0) {
    Error("mmap failed, privileges");
    return MAP_FAILED;
  }

  as = &current->as;
  addr = ALIGN_DOWN((vm_addr)_addr, PAGE_SIZE);
  len = ALIGN_UP(len, PAGE_SIZE);
  offset = ALIGN_DOWN(offset, PAGE_SIZE);
  
  flags = (flags & ~VM_SYSTEM_MASK) | MAP_USER | prot;
  
  mr = memregion_create(as, addr, len, flags, MR_TYPE_ALLOC);
  
  if (mr == NULL) {
    Error("mmap failed memregion_create");
    return MAP_FAILED;
  }

  addr = mr->base_addr;

  if (flags & MAP_PHYS) {
    paddr = (vm_addr)offset;
    
    for (va = addr, pa = paddr; va < addr + len; va += PAGE_SIZE, pa += PAGE_SIZE) {
      if (pmap_enter(as, va, pa, flags) != 0) {
        Warn("pmap_enter in mmapPhys failed");
        goto cleanup;
      }
    }
  } else {
    for (va = addr; va < addr + len; va += PAGE_SIZE) {
      if ((pf = alloc_pageframe(PAGE_SIZE)) == NULL) {
        goto cleanup;
      }

      if (pmap_enter(as, va, pf->physical_addr, flags) != 0) {
        goto cleanup;
      }
      
      pf->reference_cnt = 1;
    }
  }

  pmap_flush_tlbs();
  
  Info("%08x = sys_mmap(len:%d, flags:%08x)", (uint32_t)addr, len, flags);

  return (void *)addr;

cleanup:
  ceiling = va;

  if (flags & MAP_PHYS) {
    for (va = addr; va < ceiling; va += PAGE_SIZE) {
      if (pmap_extract(as, va, &paddr, NULL) == 0) {
        pmap_remove(as, va);
      }
    }
  } else {
    for (va = addr; va < ceiling; va += PAGE_SIZE) {
      if (pmap_extract(as, va, &paddr, NULL) == 0) {
        pf = pmap_pa_to_pf(paddr);
        free_pageframe(pf);
        pmap_remove(as, va);
      }
    }
  }
  
  pmap_flush_tlbs();
  memregion_free(as, addr, len);
  return MAP_FAILED;
}


/* @brief   Free an area of memory belonging to a process
 *
 * @param   _addr, start address of region to free
 * @param   len, size of region to free
 * @return  0 on success, negative errno on failure
 */
int sys_munmap(void *_addr, size_t len)
{
  struct Process *current;
  struct AddressSpace *as;
  vm_addr addr;
  vm_addr va;

  Info("sys_unmap(addr:%08x, len:%u)", (uint32_t)_addr, len);

  current = get_current_process();

  as = &current->as;
  addr = ALIGN_DOWN((vm_addr)_addr, PAGE_SIZE);
  len = ALIGN_UP(len, PAGE_SIZE);

  for (va = addr; va < addr + len; va += PAGE_SIZE) {
    // FIXME: Get pageframe (pmap_extract?)  decrement ref count.  Free page if ref_cnt == 0
    // Finish FreePageframe
    
    pmap_remove(as, va);
  }

  pmap_flush_tlbs();  
  memregion_free(as, addr, len);
  return 0;
}


/* @brief   Change the protection attributes of a region of the address space
 * 
 * Changes the read/write/execute protection attributes of a range of pages in
 * the current address space
 */
int sys_mprotect(void *_addr, size_t len, int prot)
{
  struct Process *current;
  struct AddressSpace *as;
  vm_addr addr;
  vm_addr va;
  vm_addr pa;
  uint64_t privileges;
  int flags;
  
#if 0

  current = get_current_process();

  if (prot & PROT_EXEC) {
    privileges = PRIV_VPROTECT | PRIV_VPROTEXEC;
  } else {
    privileges = PRIV_VPROTECT;
  }

  if (check_privileges(current, privileges) != 0) {
    return -EPERM;
  }

  as = &current->as;
  addr = ALIGN_DOWN((vm_addr)_addr, PAGE_SIZE);
  len = ALIGN_UP(len, PAGE_SIZE);

  memregion_split(as, addr);
  memregion_split(as, addr + len);

  for (va = addr; va < addr + len; va += PAGE_SIZE) {
    if (pmap_is_page_present(as, va) == false) {
      continue;
    }

    if (pmap_extract(as, va, &pa, &flags) != 0) {
      break;
    }

    // TODO: Mask off the protections, apply new protections

    if ((flags & MAP_PHYS) != MAP_PHYS && (flags & PROT_WRITE)) {
      // Read-Write mapping

#if 0
      flags |= MAP_COW;			// FIXME: Could be single ref page
#endif
      
      if (pmap_protect(as, va, flags) != 0) {
        break;
      }
    } else if ((flags & MAP_PHYS) != MAP_PHYS) {
      // Read-only mapping
      if (pmap_protect(as, va, flags) != 0) {
        break;
      }
    } else {
      // Physical Mapping
      if (pmap_protect(as, va, flags) != 0) {
        break;
      }
    }
  }

  pmap_flush_tlbs();
#endif
  return 0;
}


/* @brief   Convert a user-mode virtual address to the page's physical address
 *
 * @param   addr, virtual address to convert
 * @return  physical address of the page
 *
 * To be used by drivers using DMA.
 */
vm_addr sys_virtualtophysaddr(vm_addr addr)
{
  struct Process *current;
  struct AddressSpace *as;
  vm_addr va;
  vm_addr pa;
  uint32_t flags;
 
  current = get_current_process();
  as = &current->as;
  va = ALIGN_DOWN(addr, PAGE_SIZE);
 
  if (check_privileges(current, PRIV_VALLOCPHYS) != 0) {
    Warn("VirtualToPhysAddr failed, privileges");
    return (vm_addr)NULL;
  }
 
  if (pmap_is_page_present(as, va) == false) {
    return (vm_addr)NULL;
  }

  if (pmap_extract(as, va, &pa, &flags) != 0) {
    return (vm_addr)NULL;
  }

  return pa;
}

