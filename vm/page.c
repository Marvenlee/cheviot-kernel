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
 * Functions for allocating physical pages of system RAM.
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


/* @brief   Allocate a page in kernel memory
 *
 * @return  Virtual address of an allocated, page-sized area of memory
 *
 * TODO: Rename to kmalloc_slab(slab_manager_t *slab_manager, size_t size)
 * slabs can be 4k, 16k or 64k.  Can belong to a slab manager group of pages
 * or global.
 */ 
void *kmalloc_page(void)
{
  void *vaddr;
  struct Page *page;
  
  page = alloc_page();
  
  if (page == NULL) {
    Error("kmalloc_page() failed");
    return NULL;
  }
  
  vaddr = (void *)pmap_page_to_va(page);

//  Info("kmalloc_page() vaddr:%08x", (uint32_t)vaddr);

  return vaddr;
}


/*
 *
 */
void kfree_page(void *vaddr)
{
  struct Page *page;
  
  page = pmap_va_to_page((vm_addr)vaddr);
  
  if (page != NULL) {
    free_page(page);
  }
}


/* @brief   Allocate a 4k page of physical memory
 *
 * Rename to do_alloc_page,  remove any splitting/coalescing
 */
struct Page *alloc_page(void)
{
  struct Page *page = NULL;

  Info("alloc_page()");

  page = getblk_anon();
  
  if (page == NULL) {
    Info("alloc_page failed");
    return NULL;
  }
  
  page->mflags = PGF_INUSE;
  page->reference_cnt = 0;

  pmap_page_init(&page->pmap_page);

//  Info("alloc_page() done :%08x", (uint32_t) page);

  return page;
}


/*
 *
 */
int ref_page(struct Page *page)
{
  page->reference_cnt++;
  return 0;
}


/*
 *
 */
void free_page(struct Page *page)
{
  KASSERT(page != NULL);
  KASSERT((page - page_table) < max_page);

  page->reference_cnt--;

  if (page->reference_cnt > 0) {
    return;
  }

  putblk_anon(page);
}



