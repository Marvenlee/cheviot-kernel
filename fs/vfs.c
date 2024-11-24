/*
 * Copyright 2014  Marven Gilhespie
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
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
 * Functions to create the messages sent to filesystem handler and device driver
 * message ports on file system commands.
 */

//#define KDEBUG 1

#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/vm.h>
#include <kernel/utility.h>
#include <sys/iorequest.h>
#include <string.h>


/* @brief   Lookup a file within a directory
 *
 * @param   dvnode, directory in which to search
 * @param   name, filename to look for
 * @param   result, location to store vnode pointer of the found file
 * @return  0 on success, negative errno on failure
 */
int vfs_lookup(struct VNode *dvnode, char *name, struct VNode **result) 
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  iorequest_t req = {0};
  ioreply_t reply = {0};
  msgiov_t siov[1];
  size_t name_sz;
  int sc;

  KASSERT(dvnode != NULL);
  KASSERT(name != NULL);
  KASSERT(result != NULL);  
  
  sb = dvnode->superblock;
  name_sz = StrLen(name) + 1;

  req.cmd = CMD_LOOKUP;
  req.args.lookup.dir_inode_nr = dvnode->inode_nr;
  req.args.lookup.name_sz = name_sz;

  siov[0].addr = name;
  siov[0].size = name_sz;
    
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);

  if (sc != 0) {
    Error("vfs_lookup failed, sc:%d", sc);
    *result = NULL;
    return sc;
  }
  
  if (reply.args.lookup.inode_nr == dvnode->inode_nr) {
    vnode_inc_ref(dvnode);
    vnode = dvnode;
  } else {
    vnode = vnode_get(dvnode->superblock, reply.args.lookup.inode_nr);
  }

  if (vnode == NULL) {
    vnode = vnode_new(sb, reply.args.lookup.inode_nr);

    if (vnode == NULL) {
      Info("vfs_lookup, vnode_new -ENOMEM");
      return -ENOMEM;
    }

    vnode->nlink = 1;           // FIXME: Need to get nlink.
    vnode->reference_cnt = 1;
    vnode->size = reply.args.lookup.size;      
    vnode->uid = reply.args.lookup.uid;  
    vnode->gid = reply.args.lookup.gid;
    vnode->mode = reply.args.lookup.mode;
    vnode->flags = V_VALID;
  } 

  *result = vnode;
  return 0;
}


/* @brief   Create a file
 *
 * TODO: Merge with lookup.
 */
int vfs_create(struct VNode *dvnode, char *name, int oflags,
                             struct stat *stat, struct VNode **result)
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  iorequest_t req = {0};
  ioreply_t reply = {0};
  msgiov_t siov[1];
  size_t name_sz;
  int sc;

  KASSERT(dvnode != NULL);
  KASSERT(name != NULL);
  KASSERT(result != NULL);  
  
  sb = dvnode->superblock;
  name_sz = StrLen(name) + 1;

  req.cmd = CMD_CREATE;
  req.args.create.dir_inode_nr = dvnode->inode_nr;
  req.args.create.name_sz = name_sz;
  req.args.create.oflags = oflags;
  req.args.create.mode = stat->st_mode;
  req.args.create.uid = stat->st_uid;
  req.args.create.gid = stat->st_gid;

  siov[0].addr = name;
  siov[0].size = name_sz;
    
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);

  if (sc != 0) {
    Error("vfs_create failed, sc:%d", sc);
    *result = NULL;
    return sc;
  }
  
  vnode = vnode_new(sb, reply.args.create.inode_nr);

  if (vnode == NULL) {
    Error("vfs_create, vnode_new -ENOMEM");
    return -ENOMEM;
  }

  vnode->nlink = 1;           // FIXME: Need to get nlink. 
  vnode->reference_cnt = 1;
  vnode->size = reply.args.create.size;      
  vnode->uid = reply.args.create.uid;  
  vnode->gid = reply.args.create.gid;
  vnode->mode = reply.args.create.mode;
  vnode->flags = V_VALID;

  *result = vnode;
  return 0;
}


/*
 *
 */
int vfs_sendmsg(struct VNode *vnode, int subclass, int siov_cnt, msgiov_t *siov, 
                    int riov_cnt, msgiov_t *riov, size_t sbuf_total_sz, size_t rbuf_total_sz)
{
  iorequest_t req = {0};
  int nbytes_response;
  struct SuperBlock *sb;
  
  sb = vnode->superblock;
  
  req.cmd = CMD_SENDMSG;
  req.args.sendmsg.inode_nr = vnode->inode_nr;
  req.args.sendmsg.subclass = subclass;
  req.args.sendmsg.ssize = sbuf_total_sz;
  req.args.sendmsg.rsize = rbuf_total_sz;
  
  nbytes_response = ksendmsg(&sb->msgport, IPCOPY, &req, NULL, siov_cnt, siov, riov_cnt, riov);
  return nbytes_response;
}



/* @brief   Read from a file or a device
 */
ssize_t vfs_read(struct VNode *vnode, int ipc, void *dst, size_t nbytes, off64_t *offset)
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  msgiov_t riov[1];
  int nbytes_read;

  KASSERT(vnode != NULL);
  KASSERT(dst != NULL);
        
  sb = vnode->superblock;

  req.cmd = CMD_READ;
  req.args.read.inode_nr = vnode->inode_nr;
  
  if (offset != NULL) {
    req.args.read.offset = *offset;
  } else {
    req.args.read.offset = 0;
  }

  req.args.read.sz = nbytes;
  
  riov[0].addr = dst;
  riov[0].size = nbytes;

  nbytes_read = ksendmsg(&sb->msgport, ipc, &req, NULL, 0, NULL, NELEM(riov), riov);

  if (nbytes_read < 0) {
    Error("vfs_read failed :%d", nbytes_read);
    return nbytes_read;
  }

  if (offset != NULL) {
    *offset += nbytes_read;
  }

  return nbytes_read;
}


/* @brief   Synchronous write to a file or device
 */
ssize_t vfs_write(struct VNode *vnode, int ipc, void *src, size_t nbytes, off64_t *offset)
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  msgiov_t siov[1];
  int nbytes_written;

  sb = vnode->superblock;

  req.cmd = CMD_WRITE;
  req.args.write.inode_nr = vnode->inode_nr;

  if (offset != NULL) {
    req.args.write.offset = *offset;
  } else {
    req.args.write.offset = 0;
  }

  req.args.write.sz = nbytes;

  siov[0].addr = src;
  siov[0].size = nbytes;

  nbytes_written = ksendmsg(&sb->msgport, ipc, &req, NULL, NELEM(siov), siov, 0, NULL);
  
  if (nbytes_written < 0) {
    return nbytes_written;  
  }
      
  if (offset != NULL) {
    *offset += nbytes_written;
  }

  return nbytes_written;
}


/*
 *
 */
int vfs_readdir(struct VNode *vnode, void *dst, size_t nbytes, off64_t *cookie)
{
  iorequest_t req = {0};
  ioreply_t reply = {0};
  struct SuperBlock *sb;
  msgiov_t riov[1];
  int nbytes_read;

  sb = vnode->superblock;

  req.cmd = CMD_READDIR;
  req.args.readdir.inode_nr = vnode->inode_nr;
  req.args.readdir.offset = *cookie;
  req.args.readdir.sz = nbytes;
  
  riov[0].addr = dst;
  riov[0].size = nbytes;

  nbytes_read = ksendmsg(&sb->msgport, IPCOPY, &req, &reply, 0, NULL, NELEM(riov), riov);
  
  if (nbytes_read < 0) {
    return nbytes_read;
  }

  *cookie = reply.args.readdir.offset;
  return nbytes_read;
}


/*
 * FIXME: Need to allocate vnode but not set INODE nr, then send message to server.
 * Otherwise could fail to allocate after sending.
 */
int vfs_mknod(struct VNode *dvnode, char *name, struct stat *stat, struct VNode **result)
{
  iorequest_t req = {0};
  ioreply_t reply = {0};
  struct SuperBlock *sb;
  msgiov_t siov[1];
  struct VNode *vnode = NULL;
  int sc;

  sb = dvnode->superblock;

  req.cmd = CMD_MKNOD;
  req.args.mknod.dir_inode_nr = dvnode->inode_nr;
  req.args.mknod.name_sz = StrLen(name) + 1;
  // req.args.mknod.size = 0;
  req.args.mknod.uid = stat->st_uid;
  req.args.mknod.gid = stat->st_gid;
  req.args.mknod.mode = stat->st_mode;
  
  // TODO: Copy stat fields to req
  
  siov[0].addr = name;
  siov[0].size = req.args.mknod.name_sz;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);
  
  if (sc < 0) {
    return sc;
  }

  vnode = vnode_new(sb, reply.args.mknod.inode_nr);

  if (vnode == NULL) {
    return -ENOMEM;
  }

  vnode->nlink = 1;      
  vnode->reference_cnt = 1;
  vnode->size = reply.args.mknod.size;      
  vnode->uid = reply.args.mknod.uid;  
  vnode->gid = reply.args.mknod.gid;
  vnode->mode = reply.args.mknod.mode;
  vnode->flags = V_VALID;

  *result = vnode;
  return sc;
}


/*
 *
 */
int vfs_mkdir(struct VNode *dvnode, char *name, struct stat *stat, struct VNode **result)
{
  iorequest_t req = {0};
  ioreply_t reply = {0};
  struct SuperBlock *sb;
  msgiov_t siov[1];
  struct VNode *vnode = NULL;
  int sc;

  sb = dvnode->superblock;

  req.cmd = CMD_MKDIR;
  req.args.mkdir.dir_inode_nr = dvnode->inode_nr;
  req.args.mkdir.name_sz = StrLen(name) + 1;
  // req.args.mknod.size = 0;
  req.args.mkdir.uid = stat->st_uid;
  req.args.mkdir.gid = stat->st_gid;
  req.args.mkdir.mode = stat->st_mode;
  
  // TODO: Copy stat fields to req
  
  siov[0].addr = name;
  siov[0].size = req.args.mkdir.name_sz;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);
  
  if (sc < 0) {
    return sc;
  }

  vnode = vnode_new(sb, reply.args.mkdir.inode_nr);

  if (vnode == NULL) {
    return -ENOMEM;
  }

  vnode->nlink = 1;      
  vnode->reference_cnt = 1;
  vnode->size = reply.args.mknod.size;      
  vnode->uid = reply.args.mknod.uid;  
  vnode->gid = reply.args.mknod.gid;
  vnode->mode = reply.args.mknod.mode;
  vnode->flags = V_VALID;

  *result = vnode;
  return sc;
}


/*
 *
 */
int vfs_rmdir(struct VNode *dvnode, char *name)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[1];
  int sc;
  
  sb = dvnode->superblock;

  req.cmd = CMD_RMDIR;
  req.args.rmdir.dir_inode_nr = dvnode->inode_nr;
  req.args.rmdir.name_sz = StrLen(name) + 1;

  siov[0].addr = name;
  siov[0].size = req.args.rmdir.name_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, NELEM(siov), siov, 0, NULL);  
  return sc;
}


/*
 *
 */
int vfs_truncate(struct VNode *vnode, size_t size)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  int sc;
  int sz;

  Info("vfs_truncate, size:%u", size);
  sb = vnode->superblock;

  req.cmd = CMD_TRUNCATE;
  req.args.truncate.inode_nr = vnode->inode_nr;
  req.args.truncate.size = size;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req,  NULL, 0, NULL, 0, NULL);
  return sc;
}


/*
 *
 */
int vfs_rename(struct VNode *src_dvnode, char *src_name,
               struct VNode *dst_dvnode, char *dst_name)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[2];
  struct VNode *vnode = NULL;
  int sc;

  sb = src_dvnode->superblock;

  req.cmd = CMD_RENAME;
  req.args.rename.src_dir_inode_nr = src_dvnode->inode_nr;
  req.args.rename.dst_dir_inode_nr = dst_dvnode->inode_nr;
  req.args.rename.src_name_sz = StrLen(src_name) + 1;
  req.args.rename.dst_name_sz = StrLen(dst_name) + 1;
 
  siov[0].addr = src_name;
  siov[0].size = req.args.rename.src_name_sz;
  siov[1].addr = dst_name;
  siov[1].size = req.args.rename.dst_name_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, NELEM(siov), siov, 0, NULL);  
  return sc;
}


/*
 *
 */
int vfs_chmod(struct VNode *vnode, mode_t mode)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  int sc;
  
  sb = vnode->superblock;

  req.cmd = CMD_CHMOD;
  req.args.chmod.inode_nr = vnode->inode_nr;
  req.args.chmod.mode = mode;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);
  return sc;
}


/*
 *
 */
int vfs_chown(struct VNode *vnode, uid_t uid, gid_t gid)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  int sc;
  
  sb = vnode->superblock;

  req.cmd = CMD_CHOWN;
  req.args.chown.inode_nr = vnode->inode_nr;
  req.args.chown.uid = uid;
  req.args.chown.gid = gid;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);
  return sc;
}


/*
 * Does this call vnode_put, or is it higher layer 
 * is the put done after the vfs_unlink ?  We should be the only reference
 * when this is called.
 */
int vfs_unlink(struct VNode *dvnode, char *name)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[1];
  int sc;
  
  sb = dvnode->superblock;

  req.cmd = CMD_UNLINK;
  req.args.unlink.dir_inode_nr = dvnode->inode_nr;
  req.args.unlink.name_sz = StrLen(name) + 1;

  siov[0].addr = name;
  siov[0].size = req.args.unlink.name_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, NELEM(siov), siov, 0, NULL);
  return sc;
}


/*
 *
 */
int vfs_mklink(struct VNode *dvnode, char *name, char *link, struct stat *stat)
{
	Error("vfs_mklink -ENOTSUP");
  return -ENOTSUP;
}


/*
 *
 */
int vfs_rdlink(struct VNode *vnode, char *buf, size_t sz)
{
	Error("vfs_rdlink -ENOTSUP");
  return -ENOTSUP;
}


/*
 *
 */
int vfs_fsync(struct VNode *vnode)
{
	Error("vfs_fsync -ENOTSUP");
  return -ENOTSUP;
}


/*
 *
 */
int vfs_isatty(struct VNode *vnode)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  int sc;
  
  sb = vnode->superblock;

  req.cmd = CMD_ISATTY;
  req.args.isatty.inode_nr = vnode->inode_nr;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);
  return sc;
}


/* @brief   Read from a file or a device
 */
ssize_t vfs_readv(struct VNode *vnode, int ipc, msgiov_t *riov, int riov_cnt, size_t nbytes, off64_t *offset)
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  int nbytes_read;

  KASSERT(vnode != NULL);
        
  sb = vnode->superblock;

  req.cmd = CMD_READ;
  req.args.read.inode_nr = vnode->inode_nr;
  
  if (offset != NULL) {
    req.args.read.offset = *offset;
  } else {
    req.args.read.offset = 0;
  }

  req.args.read.sz = nbytes;
  
  nbytes_read = ksendmsg(&sb->msgport, ipc, &req, NULL, 0, NULL, riov_cnt, riov);

  if (nbytes_read < 0) {
    Error("vfs_read failed :%d", nbytes_read);
    return nbytes_read;
  }

  if (offset != NULL) {
    *offset += nbytes_read;
  }

  return nbytes_read;
}


/* @brief   Synchronous write to a file or device
 */
ssize_t vfs_writev(struct VNode *vnode, int ipc, msgiov_t *siov, int siov_cnt, size_t nbytes, off64_t *offset)
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  int nbytes_written;

  KASSERT(vnode != NULL);

  sb = vnode->superblock;

  req.cmd = CMD_WRITE;
  req.args.write.inode_nr = vnode->inode_nr;

  if (offset != NULL) {
    req.args.write.offset = *offset;
  } else {
    req.args.write.offset = 0;
  }

  req.args.write.sz = nbytes;

  nbytes_written = ksendmsg(&sb->msgport, ipc, &req, NULL, siov_cnt, siov, 0, NULL);
  
  if (nbytes_written < 0) {
    return nbytes_written;  
  }
      
  if (offset != NULL) {
    *offset += nbytes_written;
  }

  return nbytes_written;
}

