#ifndef MSG_H
#define MSG_H

#include <kernel/error.h>
#include <kernel/lists.h>
#include <kernel/types.h>
#include <kernel/sync.h>
#include <kernel/kqueue.h>
#include <sys/syscalls.h>
#include <unistd.h>


// Forward declarations
struct Process;
struct Msg;
struct MsgBacklog;
struct MsgPort;

// List types
LIST_TYPE(Msg, msg_list_t, msg_link_t);

// Constants
#define MAX_MSG_BACKLOG       32


/* @brief   Kernel Message
 */
struct Msg
{
  msg_link_t link;
  msgid_t msgid;
  struct MsgPort *port;       // The port it is attached to or NULL if not attached.
                              // Set to reply_port on reply, so any msgid_to_msg fails after replymsg
                              
  struct MsgPort *reply_port; // The reply port to reply to
  
  int ipc;
  struct AddressSpace *src_as;
  
  int reply_status;  
  int siov_cnt;
  msgiov_t *siov;
  int riov_cnt;
  msgiov_t *riov;
};


/* @brief		MsgID to Msg lookup table for a message port.
 *
 */
struct MsgBacklog
{
	int backlog_sz;
	uint32_t free_bitmap;
	struct Msg *msg[MAX_MSG_BACKLOG];
};


/* @brief   Message Port for interprocess communication
 */
struct MsgPort
{
  struct Rendez rendez;
  msg_list_t pending_msg_list;
  knote_list_t knote_list;    
  
  msgidset_t inflight_msgids;
  msgidset_t abort_msgids;

  void *context;              // For pointer to superblock or other data
};



// ksendmsg options
#define KUCOPY    0     // Message is from kernel to user or user to kernel
#define IPCOPY    1     // Message is from user to user (except for fsreq header which is from kernel)     


// Macros
#define NELEM(a) (sizeof(a) / sizeof(*a))   // Calculate number of elements in an array


/*
 * Prototypes
 */
int sys_sendrec(int fd, int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov);
int sys_getmsg(int server_fd, msgid_t *msgid, void *buf, size_t buf_sz);
int sys_replymsg(int server_fd, msgid_t msgid, int status, void *buf, size_t buf_sz);
int sys_readmsg(int server_fd, msgid_t msgid, void *buf, size_t buf_sz, off_t offset);
int sys_writemsg(int server_fd, msgid_t msgid, void *buf, size_t buf_sz, off_t offset);

int kabortmsg(struct MsgPort *msgport, struct Msg *msg);
int ksendmsg(struct MsgPort *msgport, int ipc, int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov);

int kputmsg(struct MsgPort *msgport, struct Msg *msg);
int kreplymsg(struct Msg *msg);
struct Msg *kgetmsg(struct MsgPort *port);
struct Msg *kpeekmsg(struct MsgPort *port);
void kremovemsg(struct MsgPort *msgport, struct Msg *msg);

int kwaitport(struct MsgPort *msgport, struct timespec *timeout);

int seekiov(int iov_cnt, msgiov_t *iov, off_t offset, int *i, size_t *iov_remaining);

void assign_msgid(struct MsgBacklog *backlog, msgid_t msgid, struct Msg *msg);
struct Msg *msgid_to_msg(struct MsgBacklog *backlog, msgid_t msgid);
msgid_t alloc_msgid(struct MsgBacklog *backlog);
void free_msgid(struct MsgBacklog *backlog, msgid_t msgid);

int init_msgport(struct MsgPort *msgport);
int fini_msgport(struct MsgPort *msgport);
int init_msgbacklog(struct MsgBacklog *msgbacklog, int backlog);

#endif

