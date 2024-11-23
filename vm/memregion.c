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
 * Address space management of regions created by mmap
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
#include <sys/mman.h>


/*
 * MemRegionFindFree();
 */ 
struct MemRegion *memregion_find_free(struct AddressSpace *as, vm_addr addr)
{
  struct MemRegion *mr;
  
  if (as->hint != NULL && as->hint->base_addr <= addr && addr < as->hint->ceiling_addr) {
    return as->hint;
  }

  mr = LIST_HEAD(&as->free_memregion_list);
  
  while (mr != NULL) {
    if (mr->base_addr <= addr && addr < mr->ceiling_addr) {
      break;
    }
    
    mr = LIST_NEXT(mr, free_link);
  }

  if (mr != NULL) {
    as->hint = mr;
  }
  
  return mr;
}


/*
 * MemRegionFindSorted();
 */
struct MemRegion *memregion_find_sorted(struct AddressSpace *as, vm_addr addr)
{
  struct MemRegion *mr;
  
  if (as->hint != NULL && as->hint->base_addr <= addr && addr < as->hint->ceiling_addr) {
    return as->hint;
  }
  
  mr = LIST_HEAD (&as->sorted_memregion_list);

  while (mr != NULL) {
    if (mr->base_addr <= addr && addr < mr->ceiling_addr) {
      break;
    }
    
    mr = LIST_NEXT (mr, sorted_link);
  }

  if (mr != NULL) {
    as->hint = mr;
  }
  
  return mr;
}


/*
 * MemRegionCreate();
 */
struct MemRegion *memregion_create(struct AddressSpace *as, vm_offset addr,
                                   vm_size size, uint32_t flags, uint32_t type)
{
  struct MemRegion *mr, *mrbase, *mrtail;
  vm_offset aligned_base_addr;
  
  Info("memregion_create(addr:%08x, size:%08x, flags:%08x, type:%d)", (uint32_t)addr, (uint32_t)size, flags, type);
    
  if (flags & MAP_FIXED) {
    if ((mr = memregion_find_free(as, addr)) != NULL) {
      if (((addr + size) > mr->ceiling_addr) || (mr->type != MR_TYPE_FREE)) {
        mr = NULL;
      }
    }
  }  else {
    mr = LIST_HEAD (&as->free_memregion_list);
    
    while (mr != NULL) {
      aligned_base_addr = ALIGN_UP (mr->base_addr, PAGE_SIZE);
      
      if (mr->type == MR_TYPE_FREE && mr->base_addr <= aligned_base_addr &&
          aligned_base_addr < mr->ceiling_addr && size <= (mr->ceiling_addr - aligned_base_addr)) {
        addr = aligned_base_addr;
        break;
      }

      mr = LIST_NEXT (mr, free_link);
    }
  }
  
  if (mr == NULL) {
    Error("memregion_create failed mr==null");
    return NULL;
  }
  
  /* Allocate base and tail MemRegions beforehand */    
  if ((mrbase = LIST_HEAD (&unused_memregion_list)) != NULL) {
    LIST_REM_HEAD (&unused_memregion_list, unused_link);
  } else {
    Error("memregion_create failed mrbase");
    return NULL;
  }
  
  if ((mrtail = LIST_HEAD (&unused_memregion_list)) != NULL) {
    LIST_REM_HEAD (&unused_memregion_list, unused_link);
  } else {
    LIST_ADD_HEAD(&unused_memregion_list, mrbase, unused_link);
    Error("memregion_create failed mrtail");
    return NULL;
  }
                    
  if (mr->base_addr < addr) {
    /* Keep and initialise mrbase */          
    LIST_ADD_HEAD (&as->free_memregion_list, mrbase, free_link);
    LIST_INSERT_BEFORE(&as->sorted_memregion_list, mr, mrbase, sorted_link);

    mrbase->base_addr = mr->base_addr;
    mrbase->ceiling_addr = addr;
    mrbase->as = as;
    mrbase->type = MR_TYPE_FREE;
    mrbase->flags = 0;
  } else {
    /* Do not need mrbase */
    LIST_ADD_HEAD (&unused_memregion_list, mrbase, unused_link);
  }
      
  if (addr + size < mr->ceiling_addr) {
    /* Keep and initialise mrtail */
    LIST_ADD_HEAD (&as->free_memregion_list, mrtail, free_link);
    LIST_INSERT_AFTER(&as->sorted_memregion_list, mr, mrtail, sorted_link);

    mrtail->base_addr = addr + size;
    mrtail->ceiling_addr = mr->ceiling_addr;

    mrbase->as = as;
    
    mrtail->type  = MR_TYPE_FREE;
    mrtail->flags = 0;

  } else {
    /* Do not need mrtail */          
    LIST_ADD_HEAD(&unused_memregion_list, mrtail, unused_link);
  }
  
  /* Initialise new mr */  
  LIST_REM_ENTRY(&as->free_memregion_list, mr, free_link);        
  mr->base_addr = addr;
  mr->ceiling_addr = addr + size;        
  mr->as = as;
  mr->type = type;
  mr->flags = flags;

  Error("memregion_create success");
      
  return mr;
}


/* 
 * MemRegionDelete();
 */
int memregion_free(struct AddressSpace *as, vm_offset addr, vm_size size)
{
  struct MemRegion *mr, *mr_prev, *mr_next;
  int sc1, sc2;

  Info("memregion_free");
  
  sc1 = memregion_split(as, addr);
  sc2 = memregion_split(as, addr + size);

  if (sc1 != 0 || sc2 != 0) {
    return -ENOMEM;
  }

  mr = memregion_find_sorted(as, addr);
 
  if (mr == NULL) {
    return -EINVAL;
  }

  as->hint = NULL;
   
  while (mr != NULL && mr->base_addr < addr + size) {    
    if (mr->base_addr >= addr && mr->ceiling_addr <= addr + size) {
      mr_prev = LIST_PREV(mr, sorted_link);
      mr_next = LIST_NEXT(mr, sorted_link);

      if (mr_prev != NULL && mr_prev->type == MR_TYPE_FREE) {
        /* mr_prev is on AS Free list, destroy MR and extend prev_mr */
        mr_prev->ceiling_addr = mr->ceiling_addr;

        mr->as = NULL;
        mr->type = MR_TYPE_UNALLOCATED;
        LIST_REM_ENTRY (&as->sorted_memregion_list, mr, sorted_link);
        LIST_ADD_HEAD (&unused_memregion_list, mr, unused_link);
      } else {
        mr->type  = MR_TYPE_FREE;
        mr->flags = 0;
        LIST_ADD_HEAD (&as->free_memregion_list, mr, free_link);
      }
    }
    
    mr = mr_next;  
  }  

  //  If it is free, coalesce the memregion above
  if (mr != NULL && mr->type == MR_TYPE_FREE) {
    mr_prev = LIST_PREV(mr, sorted_link);

    if (mr_prev != NULL && mr_prev->type == MR_TYPE_FREE) {
      /* mr_prev is on AS Free list, destroy MR and extend prev_mr */
      mr_prev->ceiling_addr = mr->ceiling_addr;

      mr->as = NULL;
      mr->type = MR_TYPE_UNALLOCATED;
      LIST_REM_ENTRY (&as->sorted_memregion_list, mr, sorted_link);
      LIST_ADD_HEAD (&unused_memregion_list, mr, unused_link);
    }
  }

  return 0;
}


/*
 *
 */
int memregion_split(struct AddressSpace *as, vm_offset addr)
{
  struct MemRegion *mr, *new_mr;

  Info("memregion_split");

  mr = memregion_find_sorted(as, addr);

  if (mr == NULL) {
    return -EINVAL;
  }

  if (addr == mr->base_addr || addr == mr->ceiling_addr) {
    return 0;
  }

  if ((new_mr = LIST_HEAD(&unused_memregion_list)) == NULL) {
    return -ENOMEM;
  }

  as->hint = NULL;
    
  LIST_REM_HEAD(&unused_memregion_list, unused_link);

  LIST_INSERT_AFTER(&as->sorted_memregion_list, mr, new_mr, sorted_link);

  new_mr->base_addr = addr;
  new_mr->ceiling_addr = mr->ceiling_addr;
  new_mr->type = mr->type;
  new_mr->flags = mr->flags;
  new_mr->as = as;
    
  if (new_mr->type == MR_TYPE_FREE) {
    LIST_ADD_HEAD(&as->free_memregion_list, new_mr, free_link);  
  }
  
  if (new_mr->type == MR_TYPE_PHYS) { 
	  new_mr->phys_base_addr = mr->phys_base_addr + (addr - mr->base_addr);
  }	else {
    new_mr->phys_base_addr = 0;
  }
	
	mr->ceiling_addr = addr;  
  return 0;
}


/*
 *
 */
int memregion_protect(struct AddressSpace *as, vm_offset addr, vm_size size)
{
  return 0;
}


/*
 *
 */
void memregion_free_all(struct AddressSpace *as)
{
  struct MemRegion *mr;
  
  while((mr = LIST_HEAD(&as->sorted_memregion_list)) != NULL) {
    if (mr->type == MR_TYPE_FREE) {
      LIST_REM_ENTRY(&as->free_memregion_list, mr, free_link);
    }

    LIST_REM_HEAD(&as->sorted_memregion_list, sorted_link);
    
    mr->as = NULL;
    mr->type = MR_TYPE_UNALLOCATED;
    LIST_ADD_HEAD (&unused_memregion_list, mr, unused_link);      
  }

  as->hint = NULL;
}


/*
 *
 */
int init_memregions(struct AddressSpace *as)
{
  struct MemRegion *mr;

  Info("init_memregions");
  
  LIST_INIT(&as->sorted_memregion_list);
  LIST_INIT(&as->free_memregion_list);

  as->hint = NULL;
  
  if ((mr = LIST_HEAD(&unused_memregion_list)) == NULL) {
    Error("init_memregions -ENOMEM");
    return -ENOMEM;
  }
  
  LIST_REM_HEAD (&unused_memregion_list, unused_link);

  LIST_ADD_TAIL(&as->sorted_memregion_list, mr, sorted_link);
  LIST_ADD_TAIL(&as->free_memregion_list, mr, free_link);
  mr->base_addr = VM_USER_BASE;
  mr->ceiling_addr = VM_USER_CEILING;
  mr->type = MR_TYPE_FREE;
  mr->as = as;
  mr->flags = 0;
  mr->phys_base_addr = 0;
  return 0;
}


/*
 * createaddressspace() - don't initialize segment list?  OR free initial segment
 */
int fork_memregions(struct AddressSpace *new_as, struct AddressSpace *old_as)
{
  struct MemRegion *old_mr, *new_mr;

  Info("fork_memregions");

  LIST_INIT(&new_as->sorted_memregion_list);
  LIST_INIT(&new_as->free_memregion_list);  
  new_as->hint = NULL;

  old_mr = LIST_HEAD(&old_as->sorted_memregion_list);
  
  while (old_mr != NULL) {
    if ((new_mr = LIST_HEAD(&unused_memregion_list)) == NULL) {
      goto cleanup;
    }
    
    LIST_REM_HEAD (&unused_memregion_list, unused_link);
    LIST_ADD_TAIL(&new_as->sorted_memregion_list, new_mr, sorted_link);

    new_mr->base_addr = old_mr->base_addr;
    new_mr->ceiling_addr = old_mr->ceiling_addr;
    new_mr->type = old_mr->type;
    new_mr->flags = old_mr->flags;
    new_mr->phys_base_addr = old_mr->phys_base_addr;
    new_mr->as = new_as;
        
    if (new_mr->type == MR_TYPE_FREE) {
      LIST_ADD_TAIL(&new_as->free_memregion_list, new_mr, free_link);
    }

    old_mr = LIST_NEXT(old_mr, sorted_link);
  }
    
  return 0;
  
cleanup:
  memregion_free_all(new_as);
  Error("fork_memregions -ENOMEM");
  return -ENOMEM;
}


