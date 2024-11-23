#ifndef KERNEL_VM_H
#define KERNEL_VM_H

#include <sys/syscalls.h>
#include <kernel/arch.h>
#include <kernel/lists.h>
#include <kernel/types.h>

// Forward declarations
struct Pageframe;
struct MemRegion;


// Linked list types
LIST_TYPE(Pageframe, pageframe_list_t, pageframe_link_t);
LIST_TYPE(MemRegion, memregion_list_t, memregion_link_t);


// Flags used for kernel administration of pages
#define MEM_RESERVED  (0 << 28)
#define MEM_GARBAGE   (1 << 28)

//#define MEM_ALLOC     (2 << 28)
//#define MEM_PHYS      (3 << 28)

#define MEM_FREE      (4 << 28)

#define MAP_COW       (1 << 26)
#define MAP_USER      (1 << 27)

// PROT_MASK and CACHE_MASK are defined in sys/syscalls.h
#define MEM_MASK 0xF0000000
#define VM_SYSTEM_MASK (MEM_MASK | MAP_COW | MAP_USER)


// Pageframe.flags
#define PGF_INUSE       (1 << 0)
#define PGF_RESERVED    (1 << 1)
#define PGF_CLEAR       (1 << 2)
#define PGF_KERNEL      (1 << 3)
#define PGF_USER        (1 << 4)
#define PGF_PAGETABLE   (1 << 5)


// MemRegion types
#define MR_TYPE_UNALLOCATED 0
#define MR_TYPE_FREE 1
#define MR_TYPE_ALLOC 2
#define MR_TYPE_PHYS 3


/* @brief   Structure representing an area of a process's address space
 */
struct MemRegion
{
	vm_addr base_addr;
	vm_addr ceiling_addr;

	memregion_link_t sorted_link;
	memregion_link_t free_link;
	memregion_link_t unused_link;

	struct AddressSpace *as;
	
	uint32_t type;
	uint32_t flags;
	vm_addr phys_base_addr;
};


/* @brief   Structure representing a physical page of memory
 */
struct Pageframe
{
  vm_size size;                           // Pageframe is either 64k, 16k, or 4k
  vm_addr physical_addr;
//  struct MemRegion *mr;  
  int reference_cnt;                      // Count of vpage references.
  bits32_t flags;
  pageframe_link_t link;            // cache lru, busy link.  (busy and LRU on separate lists?)
  pageframe_link_t free_slab_link;
  struct PmapPageframe pmap_pageframe;
  size_t free_object_size;
  int free_object_cnt;
  void *free_object_list_head;
};


/* @brief   Address space of a process
 */
struct AddressSpace
{
  struct Pmap pmap;

  memregion_list_t sorted_memregion_list;
  memregion_list_t free_memregion_list;
  
  struct MemRegion *hint;
  
  int memregion_cnt;
};


/*
 * Prototypes
 */

// vm/addressspace.c
int create_address_space(struct AddressSpace *new_as);
int fork_address_space(struct AddressSpace *new_as, struct AddressSpace *old_as);
int cleanup_address_space(struct AddressSpace *as);
void free_address_space(struct AddressSpace *as);

// vm/bounds.c
int bounds_check(void *addr, size_t sz);
int bounds_check_kernel(void *addr, size_t sz);

// vm/ipcopy.c
ssize_t ipcopy(struct AddressSpace *dst_as, struct AddressSpace *src_as,
               void *dvaddr, void *svaddr, size_t sz);

// vm/memregion.c
struct MemRegion *memregion_find_free(struct AddressSpace *as, vm_addr addr);
struct MemRegion *memregion_find_sorted(struct AddressSpace *as, vm_addr addr);
struct MemRegion *memregion_create(struct AddressSpace *as, vm_offset addr,
                                   vm_size size, uint32_t flags, uint32_t type);
int memregion_free(struct AddressSpace *as, vm_offset addr, vm_size size);
int memregion_split(struct AddressSpace *as, vm_offset addr);
int memregion_protect(struct AddressSpace *as, vm_offset addr, vm_size size);
void memregion_free_all(struct AddressSpace *as);
int init_memregions(struct AddressSpace *as);
int fork_memregions(struct AddressSpace *new_as, struct AddressSpace *old_as);

// vm/page.c
void *kmalloc_page(void);
void kfree_page(void *vaddr);
struct Pageframe *alloc_pageframe(vm_size);
void free_pageframe(struct Pageframe *pf);
void coalesce_slab(struct Pageframe *pf);

// vm/pagefault.c
int page_fault(vm_addr addr, bits32_t access);

// vm/vm.c
void *sys_mmap(void *_addr, size_t len, int prot, int flags, int fd, off_t offset);
int sys_munmap(void *addr, size_t size);
int sys_mprotect(void *addr, size_t size, int flags);

// boards/.../arch.S
int CopyIn(void *dst, const void *src, size_t sz);
int CopyOut(void *dst, const void *src, size_t sz);
int CopyInString(void *dst, const void *src, size_t max_sz);

// boards/.../pmap.c
int pmap_create(struct AddressSpace *as);
void pmap_destroy(struct AddressSpace *as);
int pmap_supports_cache_policy(bits32_t flags);
int pmap_enter(struct AddressSpace *as, vm_addr addr, vm_addr paddr, bits32_t flags);
int pmap_remove(struct AddressSpace *as, vm_addr addr);
int pmap_protect(struct AddressSpace *as, vm_addr addr, bits32_t flags);
int pmap_extract(struct AddressSpace *as, vm_addr va, vm_addr *pa, bits32_t *flags);

// Could merge into PmapExtract, return a status flag, with -1 for no pte, -2
// for no pde etc.
bool pmap_is_pagetable_present(struct AddressSpace *as, vm_addr va);
bool pmap_is_page_present(struct AddressSpace *as, vm_addr va);

uint32_t *pmap_alloc_pagetable(void);
void pmap_free_pagetable(uint32_t *pt);

void pmap_flush_tlbs(void);
void pmap_invalidate_all(void);
void pmap_switch(struct Process *next, struct Process *current);

void pmap_pageframe_init(struct PmapPageframe *ppf);

vm_addr pmap_pf_to_pa(struct Pageframe *pf);
struct Pageframe *pmap_pa_to_pf(vm_addr pa);
vm_addr pmap_va_to_pa(vm_addr vaddr);
vm_addr pmap_pa_to_va(vm_addr paddr);
vm_addr pmap_pf_to_va(struct Pageframe *pf);
struct Pageframe *pmap_va_to_pf(vm_addr va);

int pmap_cache_enter(vm_addr addr, vm_addr paddr);
int pmap_cache_remove(vm_addr va);
int pmap_cache_extract(vm_addr va, vm_addr *pa);
int pmap_pagetable_walk(struct AddressSpace *as, uint32_t access, void *vaddr, void **rkaddr);

/*
 * VM Macros
 * TODO: Replace ALIGN_UP and ALIGN_DOWN macros with roundup and rounddown
 * from <sys/param.h>
 */

#define ALIGN_UP(val, alignment)                                               \
  ((((val) + (alignment)-1) / (alignment)) * (alignment))

#define ALIGN_DOWN(val, alignment) ((val) - ((val) % (alignment)))

#endif
