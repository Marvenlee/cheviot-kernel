#ifndef MSG_H
#define MSG_H

#include <kernel/error.h>
#include <kernel/lists.h>
#include <kernel/types.h>
#include <kernel/sync.h>
#include <kernel/kqueue.h>
#include <sys/syscalls.h>
#include <sys/iorequest.h>
#include <unistd.h>


// Forward declarations
struct Process;
struct Msg;
struct MsgPort;

// List types
LIST_TYPE(Msg, msg_list_t, msg_link_t);


/* @brief   Kernel Message
 */
struct Msg
{
  msg_link_t link;
  msgid_t msgid;
  struct MsgPort *port;       // The port it enqueued or received on.
                              // Set to reply_port on reply, so any msgid_to_msg fails after replymsg                              
  struct MsgPort *reply_port; // The reply port to reply to
  
  int ipc;                    // true if interprocess copy, false if kernel-user or user-kernel copy
  struct AddressSpace *src_as;
  iorequest_t *req;
  ioreply_t *reply;
  int reply_status;  
  int siov_cnt;
  msgiov_t *siov;
  int riov_cnt;
  msgiov_t *riov;
};



/* @brief   Message Port for interprocess communication
 */
struct MsgPort
{
  struct Rendez rendez;
  msg_list_t pending_msg_list;
  knote_list_t knote_list;    
  
  void *context;              // For pointer to superblock or other data
};



// ksendmsg options
#define KUCOPY    0     // Message is from kernel to user or user to kernel
#define IPCOPY    1     // Message is from user to user

// Macros
#define NELEM(a) (sizeof(a) / sizeof(*a))   // Calculate number of elements in an array


/*
 * Prototypes
 */
int sys_sendrec(int fd, int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov);
int sys_getmsg(int server_fd, msgid_t *msgid, iorequest_t *req, size_t req_sz);
int sys_replymsg(int server_fd, msgid_t msgid, int status, ioreply_t *reply, size_t rep_sz);
int sys_readmsg(int server_fd, msgid_t msgid, void *buf, size_t buf_sz, off_t offset);
int sys_writemsg(int server_fd, msgid_t msgid, void *buf, size_t buf_sz, off_t offset);
int sys_readmsgiov(int fd, msgid_t msgid, int iov_cnt, msgiov_t *_iov, off_t offset);
int sys_writemsgiov(int fd, msgid_t msgid, int iov_cnt, msgiov_t *_iov, off_t offset);

int kabortmsg(struct MsgPort *msgport, struct Msg *msg);
int ksendmsg(struct MsgPort *msgport, int ipc, iorequest_t *req, ioreply_t *reply,
             int siov_cnt, msgiov_t *siov, int riov_cnt, msgiov_t *riov);

int kputmsg(struct MsgPort *msgport, struct Msg *msg);
int kreplymsg(struct Msg *msg);
struct Msg *kgetmsg(struct MsgPort *port);
struct Msg *kpeekmsg(struct MsgPort *port);
void kremovemsg(struct MsgPort *msgport, struct Msg *msg);

int kwaitport(struct MsgPort *msgport, struct timespec *timeout);

int seekiov(int iov_cnt, msgiov_t *iov, off_t offset, int *ret_i, size_t *ret_remaining, off_t *ret_offset);

struct Msg *msgid_to_msg(struct MsgPort *msgport, msgid_t msgid);
int init_msgport(struct MsgPort *msgport);
int fini_msgport(struct MsgPort *msgport);

#endif

