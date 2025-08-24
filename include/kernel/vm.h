#ifndef KERNEL_VM_H
#define KERNEL_VM_H

#include <sys/syscalls.h>
#include <kernel/arch.h>
#include <kernel/lists.h>
#include <kernel/types.h>

// Forward declarations
struct Page;
struct MemRegion;
struct VNode;


// Linked list types
LIST_TYPE(Page, page_list_t, page_link_t);
LIST_TYPE(MemRegion, memregion_list_t, memregion_link_t);


// Flags used for kernel administration of pages
#define MEM_RESERVED  (0 << 28)
#define MEM_GARBAGE   (1 << 28)

#define MEM_FREE      (4 << 28)

#define MAP_COW       (1 << 26)
#define MAP_USER      (1 << 27)

// PROT_MASK and CACHE_MASK are defined in sys/syscalls.h
#define MEM_MASK        0xF0000000
#define VM_SYSTEM_MASK  (MEM_MASK | MAP_COW | MAP_USER)


// MemRegion types
#define MR_TYPE_UNALLOCATED   0
#define MR_TYPE_FREE          1
#define MR_TYPE_ALLOC         2
#define MR_TYPE_PHYS          3


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
struct Page
{
  struct Rendez rendez;

  vm_size size;                   // Pageframe is either 64k, 16k, or 4k  
  vm_addr physical_addr;          // Physical address of page (constant)
  void *vaddr;                    // Virtual address of page mapped into kernel (constant)
  
  uint32_t mflags;                // Page flags, also see flags in pmap per process.
  
  struct VNode *vnode;            // Vnode if a buffer belongs to a file
  off64_t file_offset;            // Offset within the file of the page-sized buffer that is cached

  uint32_t bflags;                // Buffer cache flags

  page_link_t free_link;
   
  page_link_t lookup_link;        // Cached buffer lookup hash table entry
                                  // The above lists, active, laundered, dirty and strategy
                                  // are on the hashed lookup link.
  
  page_link_t vnode_link;         // All pages in cache belonging to vnode
  page_link_t superblock_link;    // All pages belonging to the SuperBlock

  page_link_t tmp_link;           // Link on temporary list of pages
  
  
  // uint64_t expiration_ticks;   // Could be sent to filesystem driver as to how long it
                                  // should delay writing block out to disk.

  int reference_cnt;              // Number of references to this page.
    
  struct PmapPage pmap_page;      // Architecture-specific list of mappings in page tables  
};


// Page.mflags
#define PGF_INUSE       (1 << 0)
#define PGF_RESERVED    (1 << 1)
#define PGF_CLEAR       (1 << 2)
#define PGF_KERNEL      (1 << 3)
#define PGF_USER        (1 << 4)
#define PGF_PAGETABLE   (1 << 5)


//#define B_READAHEAD (1 << 8)  // Hint to FS Handler to read additional blocks after this block has been read.

//#define B_ERASED    (1 << 1)  // Block is zero filled.
#define B_VALID     (1 << 2)  // Valid, on lookup hash list
#define B_BUSY      (1 << 3)

#define B_ERROR     (1 << 4)  // Buf is not valid (discarded in brelse)
#define B_DISCARD   (1 << 5)

#define B_DIRTY     (1 << 10)

#define PAGE_LOOKUP_HASH_SZ   1024



/*
 * @brief   Address space of a process
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
struct Page *alloc_page(void);
void free_page(struct Page *page);
int ref_page(struct Page *page);

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
int pmap_enter(struct AddressSpace *as, vm_addr addr, vm_addr paddr, int flags);
int pmap_remove(struct AddressSpace *as, vm_addr addr);
int pmap_protect(struct AddressSpace *as, vm_addr addr, int flags);
int pmap_extract(struct AddressSpace *as, vm_addr va, vm_addr *pa, uint32_t *flags);

// Could merge into PmapExtract, return a status flag, with -1 for no pte, -2
// for no pde etc.
bool pmap_is_pagetable_present(struct AddressSpace *as, vm_addr va);
bool pmap_is_page_present(struct AddressSpace *as, vm_addr va);

uint32_t *pmap_alloc_pagetable(void);
void pmap_free_pagetable(uint32_t *pt);

void pmap_flush_tlbs(void);
void pmap_invalidate_all(void);
void pmap_switch(struct Process *next, struct Process *current);

void pmap_page_init(struct PmapPage *ppf);

vm_addr pmap_page_to_pa(struct Page *page);
struct Page *pmap_pa_to_page(vm_addr pa);
vm_addr pmap_va_to_pa(vm_addr vaddr);
vm_addr pmap_pa_to_va(vm_addr paddr);
vm_addr pmap_page_to_va(struct Page *page);
struct Page *pmap_va_to_page(vm_addr va);
struct Page *pmap_pa_to_page(vm_addr pa);

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
