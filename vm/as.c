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
 * Address space creation and deletion
 */

//#define KDEBUG 1

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


/* @brief   Initialize an address space
 *
 * @param   new_as, empty address space of child process
 * @return  0 on success, negative errno on error
 *
 * Initializes an address space and creates the page directory
 * and initializes the free memregion list of the process. Sets
 * the entire user space as free.
 */
int create_address_space(struct AddressSpace *as)
{
  if (pmap_create(as) != 0) {
    return -ENOMEM;
  }

  if (init_memregions(as) != 0) {
    pmap_destroy(as);    
    return -ENOMEM;
  }

  return 0;
}


/* @brief   Duplicate the address space during a fork
 *
 * @param   new_as, empty address space of child process
 * @param   old_as, parent address space in which to copy from
 * @return  0 on success, negative errno on error
 */
int fork_address_space(struct AddressSpace *new_as, struct AddressSpace *old_as)
{
  vm_addr vpt, va, pa;
  bits32_t flags;
  struct Pageframe *pf;

  Info ("fork address space(new_as:%08x, old_ad:%08x)", (uint32_t)new_as, (uint32_t)old_as);
	Info("new as:%08x, current as:%08x", (uint32_t)new_as, (uint32_t)old_as);

  KASSERT(new_as != NULL);
  KASSERT(old_as != NULL);

  if (pmap_create(new_as) != 0) {
    Error("faile to create pmap");
    return -1;
  }


  if (fork_memregions(new_as, old_as) != 0) {
    Error("Failed to fork memregions");
    pmap_destroy(new_as);
    free_address_space(new_as);
    return -1;
  }


  for (vpt = VM_USER_BASE_PAGETABLE_ALIGNED; vpt < VM_USER_CEILING;
       vpt += PAGE_SIZE * N_PAGETABLE_PTE) {
    
    if (pmap_is_pagetable_present(old_as, vpt) == false) {
      continue;
    }

    for (va = vpt; va < vpt + PAGE_SIZE * N_PAGETABLE_PTE; va += PAGE_SIZE) {
      if (pmap_is_page_present(old_as, va) == false) {
        continue;
      }

      if (pmap_extract(old_as, va, &pa, &flags) != 0) {
        goto cleanup;
      }
           
      if ((flags & MAP_PHYS) != MAP_PHYS && (flags & PROT_WRITE)) {
        // Read-Write mapping, Mark page in both as COW and read-only;
        flags |= MAP_COW;
     
        if (pmap_protect(old_as, va, flags) != 0) {
          goto cleanup;
        }

        if (pmap_enter(new_as, va, pa, flags) != 0) {
          goto cleanup;
        }

        pf = pmap_pa_to_pf(pa);
        pf->reference_cnt++;
        
      } else if ((flags & MAP_PHYS) != MAP_PHYS) {
        // TODO: Should flags also map it as COW?
        // Read-only mapping
        if (pmap_enter(new_as, va, pa, flags) != 0) {
          goto cleanup;
        }

        pf = pmap_pa_to_pf(pa);
        pf->reference_cnt++;
      } else {
        // Physical Mapping
#if 1
        // FIXME: physical mapping flags are being overridden here.        
      	flags =	MAP_PHYS | PROT_READ | PROT_WRITE;
#endif        
        
        if (pmap_enter(new_as, va, pa, flags) != 0) {
        	Error("**** Phys pmap enter failed *****");
          goto cleanup;
        }
      }
    }
  }

  return 0;

cleanup:
  Info ("fork address space failed, cleanup");
  cleanup_address_space(new_as);
  free_address_space(new_as);
  return -1;
}


/* @brief   Free user-space pages during process termination
 *
 * @param   as, address space to clear and free pages from.
 *
 * This initializes the memregion list to a single free region so that
 * this can be used by sys_exec() to clear the existing address space.
 */
int cleanup_address_space(struct AddressSpace *as)
{
  struct Pageframe *pf;
  vm_addr pa;
  vm_addr vpt;
  vm_addr va;
  uint32_t flags;

  for (vpt = VM_USER_BASE_PAGETABLE_ALIGNED; vpt <= VM_USER_CEILING; vpt += PAGE_SIZE * N_PAGETABLE_PTE) {
    if (pmap_is_pagetable_present(as, vpt) == false) {
      continue;
    }

    for (va = vpt; va < vpt + PAGE_SIZE * N_PAGETABLE_PTE; va += PAGE_SIZE) {
      if (pmap_is_page_present(as, va) == false) {
        continue;
      }

      if (pmap_extract(as, va, &pa, &flags) != 0) {
        continue;
      }

      if (pmap_remove(as, va) != 0) {
        continue;
      }

      if ((flags & MAP_PHYS) == MAP_PHYS) {
        continue;
      }

      pf = pmap_pa_to_pf(pa);
      pf->reference_cnt--;

      if (pf->reference_cnt == 0) {
        free_pageframe(pf);
      }
    }
  }

  memregion_free_all(as);

  if (init_memregions(as) != 0) {
    return -ENOMEM;
  }
  
  return 0;  
}


/* @brief   Frees the page directory of a process
 * @param   as, address space of process
 *
 * This is called by the parent process to free any remaining
 * memory used by a child process.  This is handled by WaitPid
 * to free the child process's resources.
 */
void free_address_space(struct AddressSpace *as)
{
  pmap_destroy(as);
  pmap_flush_tlbs();
  
  memregion_free_all(as);
}

