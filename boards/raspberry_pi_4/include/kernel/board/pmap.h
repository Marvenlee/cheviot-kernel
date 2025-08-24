/*
 * Copyright 2023  Marven Gilhespie
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

#ifndef MACHINE_BOARD_RASPBERRY_PI_4_PMAP_H
#define MACHINE_BOARD_RASPBERRY_PI_4_PMAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
struct PmapPagedir;

// Pmap pagetable list
LIST_TYPE(PmapPagedir, pmappagedir_list_t, pmappagedir_link_t);


/* @brief   Metadata for allocating page directories.
 */
struct PmapPagedir
{
  uint32_t *pagedir;
  pmappagedir_link_t free_link;
};



/*
 */
LIST_TYPE(Pmap, pmap_list_t, pmap_list_link_t);
LIST_TYPE(PmapVPTE, pmap_vpte_list_t, pmap_vpte_list_link_t);

struct Pmap
{
  uint32_t *l1_table; // Page table
};


struct PmapVPTE
{
  pmap_vpte_list_link_t link;
  uint32_t flags;
} __attribute__((packed));


struct PmapPage
{
  pmap_vpte_list_t vpte_list;
};


// Prototypes
void PmapPageFault(void);
uint32_t *PmapGetPageTable(struct Pmap *pmap, int pde_idx);



#endif

