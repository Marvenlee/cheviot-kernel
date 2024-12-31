#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

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
#include <unistd.h>
#include <sys/types.h>
#include <sys/syslimits.h>
#include <sys/syscalls.h>
#include <sys/socket.h>


int sys_accept(int socket, struct sockaddr *address, socklen_t *address_len);
int sys_bind(int socket, const struct sockaddr *address, socklen_t address_len);
int sys_connect(int socket, const struct sockaddr *address, socklen_t address_len);
int sys_getpeername(int socket, struct sockaddr *address, socklen_t *address_len);
int sys_getsockname(int socket, struct sockaddr *address, socklen_t *address_len);
int sys_getsockopt(int socket, int level, int option_name, void *option_value, socklen_t *option_len);
int sys_listen(int socket, int backlog);
ssize_t sys_recv(int socket, void *buffer, size_t length, int flags);
ssize_t sys_send(int socket, const void *message, size_t length, int flags);
int sys_setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);
int sys_shutdown(int socket, int how);
int sys_socket(int domain, int type, int protocol);

// TODO: We need to rename CheviotOS sendmsg to saymsg()

// ssize_t sendmsg(int socket, const struct msghdr *message, int flags);
//ssize_t sys_recvfrom(int socket, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len);
// ssize_t recvmsg(int socket, struct msghdr *message, int flags);


#endif
