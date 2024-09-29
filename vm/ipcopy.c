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
 */

//#define KDEBUG  1

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


/* @brief   Interprocess memory copy
 *
 * @param   dst_as, destination address space of process
 * @param   src_as, source address space of process
 * @param   dvaddr, dsetination pointer in user-space
 * @param   svaddr, source pointer in user space
 * @param   sz, size of buffer to copy
 * @return  0 on success, negative errno on error
 */
ssize_t ipcopy(struct AddressSpace *dst_as, struct AddressSpace *src_as,
               void *dvaddr, void *svaddr, size_t sz)
{
  ssize_t remaining;
  int sc;
  void *skaddr;
  void *dkaddr;
  ssize_t src_page_remaining = 0;
  ssize_t dst_page_remaining = 0;
  ssize_t chunk_sz;

  Info("ipcopy(dvaddir:%08x, svaddr:%08x, sz:%u)", (uint32_t)dvaddr, (uint32_t)svaddr, (uint32_t)sz);

  remaining = sz;

  if ((vm_addr)dvaddr >= VM_USER_CEILING || VM_USER_CEILING - (vm_addr)dvaddr < sz
      || (vm_addr)svaddr >= VM_USER_CEILING || VM_USER_CEILING - (vm_addr)svaddr < sz) {
    return -EFAULT;
  }
  
	while(remaining > 0) {
	  if (src_page_remaining == 0) {
	    if ((sc = pmap_pagetable_walk(src_as, PROT_READ, svaddr, &skaddr)) != 0) {
			  return sc;
		  }			
    }
    
    if (dst_page_remaining == 0) {
		  if ((sc = pmap_pagetable_walk(dst_as, PROT_WRITE, dvaddr, &dkaddr)) != 0) {
		    return sc;
      }
    }
        
		src_page_remaining = PAGE_SIZE - ((off_t)svaddr % PAGE_SIZE); 
		dst_page_remaining = PAGE_SIZE - ((off_t)dvaddr % PAGE_SIZE); 
		
		chunk_sz = remaining;
		
		if (src_page_remaining < chunk_sz) {
		  chunk_sz = src_page_remaining;
		}

		if (dst_page_remaining < chunk_sz) {
		  chunk_sz = dst_page_remaining;
		}

    Info("ipcopy: s_pg_rem:%u, d_pg_rem:%u, chunk_sz:%u", (uint32_t)src_page_remaining,
                                                          (uint32_t)dst_page_remaining,
                                                          chunk_sz);

		memcpy(dkaddr, skaddr, chunk_sz);
		
		svaddr += chunk_sz;
		dvaddr += chunk_sz;
		remaining -= chunk_sz;
		src_page_remaining -= chunk_sz;
		dst_page_remaining -= chunk_sz;
	}
	
	return 0;
}


