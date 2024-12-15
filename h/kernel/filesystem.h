#ifndef KERNEL_FILESYSTEM_H
#define KERNEL_FILESYSTEM_H

#include <kernel/arch.h>
#include <dirent.h>
#include <fcntl.h>
#include <kernel/error.h>
#include <kernel/lists.h>
#include <kernel/msg.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <kernel/kqueue.h>
#include <kernel/types.h>
#include <limits.h>
#include <sys/iorequest.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <sys/execargs.h>
#include <unistd.h>
#include <sys/types.h>
#include <poll.h>
#include <sys/syslimits.h>
#include <sys/select.h>
#include <sys/syscalls.h>


// Forward declarations
struct VNodeLock;
struct VNode;
struct Filp;
struct VFS;
struct DName;
struct Buf;
struct Msgport;
struct ISRHandler;
struct KQueue;
struct TTYState;

// List types
LIST_TYPE(Buf, buf_list_t, buf_link_t);
LIST_TYPE(DName, dname_list_t, dname_link_t);
LIST_TYPE(VNode, vnode_list_t, vnode_link_t);
LIST_TYPE(VFS, vfs_list_t, vfs_link_t);
LIST_TYPE(Filp, filp_list_t, filp_link_t);
LIST_TYPE(SuperBlock, superblock_list_t, superblock_link_t);
LIST_TYPE(Pipe, pipe_list_t, pipe_link_t);
LIST_TYPE(TTYState, ttystate_list_t, ttystate_link_t);
LIST_TYPE(DelWriMsg, delwrimsg_list_t, delwrimsg_link_t);


// lookup() flags
#define LOOKUP_TYPE(x)    (x & 0x0000000f)  // Lookup without parent or remove will return just matching vnode
#define LOOKUP_PARENT     (1 << 0)  // Returns parent (and if exists the matching vnode)
#define LOOKUP_REMOVE     (2 << 0)  // Returns both the parent and the vnode

#define LOOKUP_NOFOLLOW   (1 << 5)  // Don't follow last element
#define LOOKUP_KERNEL     (1 << 6)

// Unmount() flags
#define UNMOUNT_FORCE     (1 << 0)

// set_fd() flags
#define FD_FLAG_CLOEXEC   (1 << 0)

// Sizes
#define MAX_SYMLINK         32    // Limit of number of symlinks that can be followed
#define NR_DNAME            64    // Number of entries in directory name lookup cache (DNLC)
#define DNAME_SZ            64
#define MIN_READDIR_BUF_SZ  512   // Minimum readdir buffer size


// Static table sizes (TODO: Adjust based on RAM size)
#define NR_SOCKET       1024
#define NR_SUPERBLOCK   128
#define NR_FILP         1024
#define NR_VNODE        1024
#define NR_PIPE         64
#define NR_BUF          1024    // Dynamically allocate ?
#define NR_MSGID2MSG    256     // Must match NPROCESS or greater


// Buffer size and number of hash table entries
#define CACHE_PAGETABLES_CNT          256
#define CACHE_PAGETABLES_PDE_BASE     3072
#define CACHE_BASE_VA                 0xC0000000
#define CACHE_CEILING_VA              0xD0000000

#define MAX_ARGS_SZ                   0x10000 // Size of buffers for args and environment variables used during exec 
#define PIPE_BUF_SZ                   4096    // Buffer size of pipes (should be same as page size)

#define DELWRI_DELAY_TICKS            500     // Time in ticks to delay a delayed-write
#define SCHED_PRIO_CACHE_HANDLER      16      // Task priority of bdflush tasks.

#define MAX_RENAME_PATH_CHECK_DEPTH   128   /* Max directories to ascend when checking rename
                                               directory is now a subdirectory of new path */

// Hash table sizes
#define VNODE_HASH                    1024
#define BUF_HASH                      1024
#define DNAME_HASH                    32

  
/* @brief   A single cluster of a file in the file buffer cache
 */
struct Buf
{  
  struct Rendez rendez;
  bits32_t flags;
  struct VNode *vnode;

  off64_t file_offset;            // Offset within the file of the page-sized buffer that is cached
  void *data;                     // Address of the page-sized buffer holding cached file data

  buf_link_t free_link;           // Free list entry
  buf_link_t hash_link;           // Hash table entry
  buf_link_t vnode_link;          // List of all blocks belonging to a file
  buf_link_t async_link;          // bawrite or bdflush LRU list entry  

  uint64_t expiration_time;       // Time that a delayed-write block should be flushed
};

// Buf.flags
#define B_FREE      (1 << 0)  // On free list
#define B_VALID     (1 << 2)  // Valid, on lookup hash list
#define B_BUSY      (1 << 3)  // Busy
#define B_ERROR     (1 << 4)  // Buf is not valid (discarded in brelse)
#define B_DISCARD   (1 << 5)  // Discard block in brelse



// FIXME: Do we need the ones below, can these be hints to vfs_strategy?
#define B_READ      (1 << 6)  // Read block of file (upto block size, depending om file size)
#define B_WRITE     (1 << 7)  // Write block of file (upto block size, depending on file size)
#define B_ASYNC     (1 << 8)  // Hint to FS Handler that block won't be written again soon (writing next block).
#define B_DELWRI    (1 << 9)  // Hint to FS Handler that block may be written again soon
#define B_READAHEAD (1 << 10)  // Hint to FS Handler to read additional blocks after this block has been read.

// 
#define BSYNC_ALL_NOW   (0xFFFFFFFFFFFFFFFF)


/* @brief   Pipe state
 */
struct Pipe
{
  struct Rendez rendez;
  pipe_link_t link;
  void *data;
  int w_pos;
  int r_pos;
  int free_sz;
  int data_sz;
  int reader_cnt;     // Number of filps that are readers (not total FDs?)
  int writer_cnt;     // Number of filps that are writers (not total FDs?)
  int inode_nr;       // Preallocated inode number
};


/* @brief   VNode state representing a file, directory, pipe or special device.
 */
struct VNode
{
  struct RWLock lock;
  
  struct Rendez rendez;       // Non-lock related rendez, events

  bool char_read_busy;        // Allow only a single read to progress
  bool char_write_busy;       // Allow only a single write to progress

  struct SuperBlock *superblock;

  int flags;
  int reference_cnt;

  struct VNode *vnode_covered;
  struct VNode *vnode_mounted_here;
  
  struct Pipe *pipe;      // FIXME Perhaps a union of different specialist objects?
                          // Will we need something for sockets and socketpair ?
                          // Or will we have a FS handler for those?
  // bool isatty;         // FIXME: set isatty in mount()  or set in flags
  pid_t tty_sid;          // Session of a controlling TTY
  
  ino_t inode_nr;         // inode number
  mode_t mode;            // file type, protection, etc
  uid_t uid;              // user id of the file's owner
  gid_t gid;              // group id
  off64_t size;           // file size in bytes
  time_t atime;
  time_t mtime;
  time_t ctime;
  int blocks;
  int blksize;
  int rdev;
  int nlink;              // Number of hard links to inode
  
  vnode_link_t hash_link;               // hash table lookup link    
  vnode_link_t vnode_link;              // Free list or in use link
  
  buf_list_t buf_list;                  // TODO: All buffers belonging to a file
  buf_list_t pendwri_buf_list;          // List of blocks whose expiration time has expired
  buf_list_t delwri_buf_list;           // List of blocks on the delayed write list
  
  dname_list_t dname_list;              // List of all dname entries pointing to this vnode
  dname_list_t directory_dname_list;    // List of all entries within this directory

  knote_list_t knote_list;
};

// VNode.flags
#define V_FREE (1 << 1)
#define V_VALID (1 << 2)
#define V_ROOT (1 << 3)
#define V_ABORT (1 << 4)


/* @brief   SuperBlock data structure for a mounted filesystem.
 */
struct SuperBlock
{
  struct RWLock lock;

  dev_t dev;
  
  struct MsgPort msgport;

  off64_t size;
  int block_size;                       // start sector needs to be aligned with block size

  struct VNode *root;                   // Could replace with a flag to indicate root?  (what
                                        // about rename path ascension?)
  uint32_t flags;

  int reference_cnt;

  superblock_link_t link;
  vnode_list_t  vnode_list;
  
  struct Thread *bdflush_thread;
  struct Rendez bdflush_rendez;       
};

// SuperBlock.flags
#define SF_ABORT     (1 << 0)
#define SF_READONLY  (1 << 1)
#define SF_WRITETHRU (1 << 2)

// Sepcial-case SuperBlock.dev major/minor numbers
#define DEV_T_DEV_TTY   0x0500


/* @brief   Entry in the Directory Name Lookup Cache (DNLC)
 */
struct DName {
  struct VNode *dir_vnode;
  struct VNode *vnode;
  char name[DNAME_SZ];
  int hash_key;
  
  dname_link_t lru_link;
  dname_link_t hash_link;
  dname_link_t vnode_link;              // Hard links to vnode
  dname_link_t directory_link;          // Vnodes within DName's directory
};


/* @brief   File pointer of an open file
 */
struct Filp
{
  int type;
  
  union {
    struct VNode *vnode;
    struct SuperBlock *superblock;
    struct KQueue *kqueue;
  } u;
  
  off64_t offset;
  mode_t mode;                          // TODO: Need to set the access mode on file open or fcntl
  uint32_t flags;                       // Access flags, e.g. O_READ, O_WRITE
  int reference_cnt;
  filp_link_t filp_entry;               // VNode link
};

// Filp.type
#define FILP_TYPE_UNDEF        0
#define FILP_TYPE_VNODE        1
#define FILP_TYPE_SUPERBLOCK   2
#define FILP_TYPE_KQUEUE       3
#define FILP_TYPE_PIPE         5
#define FILP_TYPE_SOCKET       6
#define FILP_TYPE_SOCKETPAIR   7


/* @brief   Management of a process's file descriptors
 */
struct FProcess
{ 
  mode_t umask;
  struct VNode *current_dir;
  struct VNode *root_dir;

  fd_set fd_close_on_exec_set;          // FIXME: Should we use fd_set for close_on_exec and in-use tracking?
  fd_set fd_in_use_set;

  struct Filp *fd_table[OPEN_MAX];

};


/* @brief   Lookup state of files to vnodes
 */
struct lookupdata
{
  struct VNode *start_vnode;
  struct VNode *vnode;
  struct VNode *parent;
  char *path;                           // Page-sized buffer to hold copy of path to look up.
  char *last_component;                 // filename of last component found.
  char *position;
  char separator;
  int flags;
};


/*
 * Prototypes
 */

// fs/access.c
int sys_access(char *path, mode_t amode);
mode_t sys_umask(mode_t mode);
int sys_chmod(char *_path, mode_t mode);
int sys_chown(char *_path, uid_t uid, gid_t gid);
int check_access(struct VNode *vnode, struct Filp *filp, mode_t desired_access);
bool match_supplementary_group(struct Process *proc, gid_t gid);

// fs/block.c
ssize_t read_from_block(struct VNode *vnode, void *dst, size_t sz, off64_t *offset);
ssize_t write_to_block(struct VNode *vnode, void *dst, size_t sz, off64_t *offset);
ssize_t read_from_blockv(struct VNode *vnode, msgiov_t *iov, int iov_cnt, off64_t *offset);
ssize_t write_to_blockv(struct VNode *vnode, msgiov_t *iov, int iov_cnt, off64_t *offset);

/* fs/cache.c */
ssize_t read_from_cache(struct VNode *vnode, void *src, size_t nbytes, off64_t *offset, bool inkernel);
ssize_t write_to_cache(struct VNode *vnode, void *src, size_t nbytes, off64_t *offset);
struct Buf *bread(struct VNode *vnode, off64_t file_offset);
struct Buf *bread_zero(struct VNode *vnode, off64_t file_offset);
int bwrite(struct Buf *buf);
int bawrite(struct Buf *buf);
int bdwrite(struct Buf *buf);
void brelse(struct Buf *buf);
struct Buf *getblk(struct VNode *vnode, uint64_t file_offset);
struct Buf *findblk(struct VNode *vnode, uint64_t file_offset);
int calc_buf_hash(ino_t inode_nr, off64_t file_offset);
int bsync(struct VNode *vnode, uint64_t now);
int bsyncfs(struct SuperBlock *sb, uint64_t now);
int btruncate(struct VNode *vnode);
int bdiscard(struct Buf *buf);
int init_superblock_bdflush(struct SuperBlock *sb);
void fini_superblock_bdflush(struct SuperBlock *sb, int how);
void bdflush_task(void *arg);
struct Buf *bgetdirtybuf(struct VNode *vnode, uint64_t now);

/* fs/char.c */
int sys_isatty(int fd);
ssize_t read_from_char(struct VNode *vnode, void *src, size_t nbytes);
ssize_t write_to_char(struct VNode *vnode, void *src, size_t nbytes);                               
int sys_isatty(int fd);
int tty_fg_pgrp_check(struct VNode *vnode);
int ioctl_tcsetattr(int fd, struct termios *_termios);
int ioctl_tcgetattr(int fd, struct termios *_termios);
int ioctl_tiocsctty(int fd, int arg);
int ioctl_tiocnotty(int fd);
int ioctl_tiocgsid(int fd, pid_t *sid);
int ioctl_tiocgpgrp(int fd, pid_t *_pgrp);
int ioctl_tiocspgrp(int fd, pid_t *_pgrp);
int ioctl_setsyslog(int fd);

/* fs/dir.c */
int sys_chdir(char *path);
int sys_fchdir(int fd);
int sys_chroot(char *_new_root);
int sys_opendir(char *name);
int sys_closedir(int fd);
int sys_mkdir(char *pathname, mode_t mode);
int sys_rmdir(char *pathname);
ssize_t sys_readdir(int fd, void *dst, size_t sz);
int sys_rewinddir(int fd);

/* fs/dnlc.c */
int dname_lookup(struct VNode *dir, char *name, struct VNode **vnp);
int dname_enter(struct VNode *dir, struct VNode *vn, char *name);
int dname_remove(struct VNode *dir, char *name);
void dname_purge_vnode(struct VNode *vnode);
void dname_purge_superblock(struct SuperBlock *sb);
void dname_purge_all(void);

/* fs/exec.c */
int sys_exec(char *filename, struct execargs *args);
int copy_in_argv(char *pool, struct execargs *_args, struct execargs *args);
int copy_out_argv(void *stack_pointer, int stack_size, struct execargs *args);
char *alloc_arg_pool(void);
void free_arg_pool(char *mem);

// fs/exec_root.c */
void exec_root(void *arg);
int load_root_elf(void *file_base, void **entry_point);
int init_root_argv(char *pool, struct execargs *args, char *exe_name, void *ifs_base, size_t ifs_size);
ssize_t read_ifs_image(void *base, off_t offset, void *vaddr, size_t sz);

/* fs/filedesc.c */
int sys_fcntl(int fd, int cmd, int arg);
int sys_dup(int h);
int sys_dup2(int h, int new_h);
int sys_close(int h);
int do_close(struct Process *proc, int fd);

int dup_fd(struct Process *proc, int fd, int min_fd, int max_fd);

int close_on_exec_process_fds(void);

int alloc_fd(struct Process *proc, int min_fd, int max_fd);
int free_fd(struct Process *proc, int fd);
int set_fd(struct Process *proc, int fd, int type, uint32_t flags, void *item);

/* fs/filp.c */
int close_filp(struct Process *proc, int fd);
struct Filp *get_filp(struct Process *proc, int fd);
int alloc_fd_filp(struct Process *proc);
int free_fd_filp(struct Process *proc, int fd);
struct Filp *alloc_filp(void);
void free_filp(struct Filp *filp);

/* fs/fproc.c */
int init_fproc(struct Process *proc);
int fini_fproc(struct Process *proc);
int fork_process_fds(struct Process *newp, struct Process *oldp);

/* fs/init.c */
void init_vfs(void);
void init_vfs_lists(void);
void init_vfs_cache(void);
void init_vfs_pipes(void);

/* fs/link.c */
int sys_unlink(char *pathname);

/* fs/lookup.c */
int lookup(char *_path, int flags, struct lookupdata *ld);
void lookup_cleanup(struct lookupdata *ld);
int init_lookup(char *_path, uint32_t flags, struct lookupdata *ld);
int lookup_path(struct lookupdata *ld);
int lookup_last_component(struct lookupdata *ld);
char *path_token(struct lookupdata *ld);
bool is_last_component(struct lookupdata *ld);
int walk_component (struct lookupdata *ld);
struct VNode *path_advance(struct VNode *dvnode, char *component);

// fs/mknod.c */
int sys_mknod2(char *_handlerpath, uint32_t flags, struct stat *stat);

/* fs/mount.c */
int sys_pivotroot(char *_new_root, char *_old_root);
int sys_renamemount(char *_new_path, char *_old_path);
int sys_ismount(char *_path);
bool is_mountpoint(struct VNode *vnode);

/* fs/msgport.c */
int sys_createmsgport(char *_path, uint32_t flags, struct stat *_stat);
int sys_unmount(char *_path, uint32_t flags);
int close_msgport(struct Process *proc, int fd);
 
/* fs/open.c */
int sys_open(char *_path, int oflags, mode_t mode);
int kopen(char *_path, int oflags, mode_t mode);
int do_open(struct lookupdata *ld, int oflags, mode_t mode);

/* fs/pipe.c */
void InitPipes(void);
struct Pipe *AllocPipe(void);
void FreePipe(struct Pipe *pipe);
int sys_pipe(int _fd[2]);
ssize_t read_from_pipe (struct VNode *vnode, void *src, size_t nbytes);
ssize_t write_to_pipe (struct VNode *vnode, void *src, size_t nbytes);

/* fs/poll.c */
int sys_poll (struct pollfd *pfds, nfds_t nfds, int timeout);
int sys_pollnotify (int fd, int ino, short mask, short events);

/* fs/truncate.c */
int sys_truncate(int fd, size_t sz);

/* fs/read.c */
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t kread(int fd, void *dst, size_t sz);
ssize_t sys_blkreadv(int fd, msgiov_t *iov, int iov_cnt);

/* fs/rename.c */
int sys_rename(char *oldpath, char *newpath);

/* fs/seek.c */
off_t sys_lseek(int fd, off_t pos, int whence);
int sys_lseek64(int fd, off64_t *pos, int whence);

/* fs/superblock.c */
struct SuperBlock *get_superblock(struct Process *proc, int fd);
int alloc_fd_superblock(struct Process *proc);
int free_fd_superblock(struct Process *proc, int fd);
struct SuperBlock *alloc_superblock(void);
void register_mounted_superblock(struct SuperBlock *sb);
void free_superblock(struct SuperBlock *sb);

/* fs/sync.c */
int sys_sync(void);
int sys_fsync(int fd);

/* fs/vfs.c */
int vfs_sendmsg(struct VNode *vnode, int subclass, int siov_cnt, msgiov_t *siov, 
                    int riov_cnt, msgiov_t *riov, size_t sbuf_total_sz, size_t rbuf_total_sz);
ssize_t vfs_read(struct VNode *vnode, int ipc, void *buf, size_t nbytes, off64_t *offset);
ssize_t vfs_write(struct VNode *vnode, int ipc, void *buf, size_t nbytes, off64_t *offset);
int vfs_readdir(struct VNode *vnode, void *buf, size_t bytes, off64_t *cookie);
int vfs_lookup(struct VNode *dir, char *name, struct VNode **result);
int vfs_create(struct VNode *dvnode, char *name, int oflags, struct stat *stat, struct VNode **result);                             
int vfs_unlink(struct VNode *dvnode, struct VNode *vnode, char *name);
int vfs_truncate(struct VNode *vnode, size_t sz);
int vfs_mknod(struct VNode *dir, char *name, struct stat *stat);                            
int vfs_mklink(struct VNode *dvnode, char *name, char *link, struct stat *stat);
int vfs_rdlink(struct VNode *vnode, char *buf, size_t sz);
int vfs_rmdir(struct VNode *dir, struct VNode *vnode, char *name);
int vfs_mkdir(struct VNode *dir, char *name, struct stat *stat);
int vfs_rename(struct VNode *src_dvnode, char *src_name, struct VNode *dst_dvnode, char *dst_name);
int vfs_chmod(struct VNode *vnode, mode_t mode);
int vfs_chown(struct VNode *vnode, uid_t uid, gid_t gid);
int vfs_fsync(struct VNode *vnode);
int vfs_isatty(struct VNode *vnode);

ssize_t vfs_readv(struct VNode *vnode, int ipc, msgiov_t *riov, int riov_cnt, size_t nbytes, off64_t *offset);
ssize_t vfs_writev(struct VNode *vnode, int ipc, msgiov_t *siov, int siov_cnt, size_t nbytes, off64_t *offset);

/* fs/vnode.c */
struct VNode *get_fd_vnode(struct Process *proc, int fd);
int close_vnode(struct Process *proc, int fd);
struct VNode *vnode_new(struct SuperBlock *sb);
int calc_vnode_hash(struct SuperBlock *sb, ino_t inode_nr);
void vnode_hash_enter(struct VNode *vnode);
void vnode_hash_remove(struct VNode *vnode);
struct VNode *vnode_get(struct SuperBlock *sb, int vnode_nr);
void vnode_put(struct VNode *vnode);
void vnode_add_reference(struct VNode *vnode);
void vnode_discard(struct VNode *vnode);      // Delete a vnode from cache ???
struct VNode *vnode_find(struct SuperBlock *sb, int inode_nr);

/* fs/write.c */
ssize_t sys_write(int fd, void *buf, size_t count);


// Static asserts of the file system
// STATIC_ASSERT(sizeof(FileDescManager) <= PAGE_SIZE, "struct FileDescManager larger than page");
// STATIC_ASSERT(OPEN_MAX == FD_SETSIZE, "OPEN_MAX != FD_SETSIZE");

#endif
