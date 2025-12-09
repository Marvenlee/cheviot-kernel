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

//#define KDEBUG

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

  kassert(name != NULL);

  klog_info("vfs_lookup(dvnode:%08x, name:%s)", (uint32_t)dvnode, name);

  kassert(dvnode != NULL);
  kassert(result != NULL);  
  kassert(dvnode->superblock != NULL);

  sb = dvnode->superblock;
  name_sz = StrLen(name) + 1;

  req.cmd = CMD_LOOKUP;
  req.args.lookup.dir_inode_nr = dvnode->inode_nr;
  req.args.lookup.name_sz = name_sz;

  siov[0].addr = name;
  siov[0].size = name_sz;
    
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);

  if (sc != 0) {
    klog_error("vfs_lookup failed, sc:%d", sc);
    *result = NULL;
    return sc;
  }
  
  if (reply.args.lookup.inode_nr == dvnode->inode_nr) {
    klog_warn("lookup.inode_nr reply same as dvnode->inode_nr:%d", dvnode->inode_nr);
     
    vnode_ref(dvnode);  // Will this happen?  Onlu if "." or if root ".."
    vnode = dvnode;
  } else {
    vnode = vnode_get(dvnode->superblock, reply.args.lookup.inode_nr);
    
    if (vnode != NULL) {
      klog_info("vfs_lookup, vnode exists in mem, calling vnode_ref");
      vnode_ref(vnode);
    } else {
      klog_info("vfs_lookup, vnode doesn't exist in mem");
    }
  }

  if (vnode == NULL) {
    klog_info("vfs_lookup, allocating new vnode");
    vnode = vnode_get_new(sb);

    if (vnode == NULL) {
      klog_info("vfs_lookup, vnode_get_new -ENOMEM");
      *result = NULL;
      return -ENOMEM;
    }

    vnode->inode_nr = reply.args.lookup.inode_nr;
    vnode->size  = reply.args.lookup.size;
    vnode->uid   = reply.args.lookup.uid;
    vnode->gid   = reply.args.lookup.gid;
    vnode->mode  = reply.args.lookup.mode;
    vnode->flags = V_VALID;



    vnode_hash_enter(vnode);
  } 

  klog_info("vfs_lookup success: vnode:%08x, ino_nr:%u, ref_cnt:%d", (uint32_t)vnode, (uint32_t)vnode->inode_nr, vnode->reference_cnt);

  *result = vnode;
  return 0;
}


/* @brief   Close a VNode when the reference count reaches 0
 *
 * @param   vnode, vnode to close
 * @return  0 on success, negative errno on failure or file deleted due to no remaining links
 */
int vfs_close(struct VNode *vnode) 
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  int sc;

  klog_info("vfs_close(vnode:%08x)", (uint32_t)vnode);
  
  sb = vnode->superblock;
  
  req.cmd = CMD_CLOSE;
  req.args.close.inode_nr = vnode->inode_nr;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);

  return sc;
}


/* @brief   Create a regular file
 *
 */
int vfs_create(struct VNode *dvnode, char *name, int oflags,
                             uid_t uid, gid_t gid, mode_t mode, struct VNode **result)
{
  struct SuperBlock *sb;
  struct VNode *vnode;
  iorequest_t req = {0};
  ioreply_t reply = {0};
  msgiov_t siov[2];
  size_t name_sz;
  int sc;

  kassert(dvnode != NULL);
  kassert(name != NULL);
  kassert(result != NULL);  

  klog_info("vfs_create(dvnode:%08x, name:%s)", (uint32_t)dvnode, name);
  
  sb = dvnode->superblock;
  name_sz = StrLen(name) + 1;

  req.cmd = CMD_CREATE;
  req.args.create.dir_inode_nr = dvnode->inode_nr;
  req.args.create.name_sz = name_sz;
  req.args.create.oflags = oflags;
  req.args.create.uid = uid;
  req.args.create.gid = gid;
  req.args.create.mode = mode;
  
  siov[0].addr = name;
  siov[0].size = name_sz;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);

  if (sc != 0) {
    klog_error("vfs_create failed, sc:%d", sc);
    *result = NULL;
    return sc;
  }

  vnode = vnode_get_new(sb);

  if (vnode == NULL) {
    klog_error("vfs_create, vnode_get_new -ENOMEM");
    *result = NULL;
    return -ENOMEM;
  }

  vnode_ref(vnode);
  
  vnode->inode_nr = reply.args.create.inode_nr;
  vnode->size  = reply.args.create.size;      
  vnode->uid   = reply.args.create.uid;  
  vnode->gid   = reply.args.create.gid;
  vnode->mode  = reply.args.create.mode;
  vnode->flags = V_VALID;

  vnode_hash_enter(vnode);
  
  klog_error("vfs_create success, vnode:%08x, ino_nr:%u", (uint32_t)vnode, (uint32_t)vnode->inode_nr);
    
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
  
  klog_info("vfs_sendmsg(vnode:%08x)", (uint32_t)vnode);
  
  sb = vnode->superblock;
  
  req.cmd = CMD_SENDIO;
  req.args.sendio.inode_nr = vnode->inode_nr;
  req.args.sendio.subclass = subclass;
  req.args.sendio.ssize = sbuf_total_sz;
  req.args.sendio.rsize = rbuf_total_sz;
  
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

  klog_info("vfs_read(vnode:%08x)", (uint32_t)vnode);

  kassert(vnode != NULL);
  kassert(dst != NULL);
        
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

  klog_info("vfs_read, calling ksendmsg");

  nbytes_read = ksendmsg(&sb->msgport, ipc, &req, NULL, 0, NULL, NELEM(riov), riov);

  klog_info("vfs_read, ksendmsg replied");

  if (nbytes_read < 0) {
    klog_error("vfs_read failed :%d", nbytes_read);
    return nbytes_read;
  }

  if (offset != NULL) {
    *offset += nbytes_read;
  }

  klog_info("vfs_read: nbytes_read:%d", nbytes_read);

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

  klog_info("vfs_write(vnode:%08x)", (uint32_t)vnode);

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
 *
 * TODO: Needs to update nlink
 */
int vfs_mknod(struct VNode *dvnode, char *name, uid_t uid, gid_t gid, mode_t mode)
{
  struct SuperBlock *sb;
//  struct VNode *vnode;
  iorequest_t req = {0};
  ioreply_t reply = {0};
  msgiov_t siov[1];
  int sc;

  klog_info("vfs_mknod(dvnode:%08x, name:%s)", (uint32_t)dvnode, name);

  sb = dvnode->superblock;

  req.cmd = CMD_MKNOD;
  req.args.mknod.dir_inode_nr = dvnode->inode_nr;
  req.args.mknod.name_sz = StrLen(name) + 1;
  req.args.mknod.mode = mode;
  req.args.mknod.uid = uid;
  req.args.mknod.gid = gid;
  
  siov[0].addr = name;
  siov[0].size = req.args.mknod.name_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);
  
  return sc;
}


/*
 *
 * TODO: Needs to update nlink
 */
int vfs_mkdir(struct VNode *dvnode, char *name, uid_t uid, gid_t gid, mode_t mode)
{
  struct SuperBlock *sb;
//  struct VNode *vnode;
  iorequest_t req = {0};
  ioreply_t reply = {0};
  msgiov_t siov[1];
  int sc;

  klog_info("vfs_mkdir(dvnode:%08x, name:%s)", (uint32_t)dvnode, name);

  sb = dvnode->superblock;

  req.cmd = CMD_MKDIR;
  req.args.mkdir.dir_inode_nr = dvnode->inode_nr;
  req.args.mkdir.name_sz = StrLen(name) + 1;
  req.args.mkdir.uid = uid;
  req.args.mkdir.gid = gid;
  req.args.mkdir.mode = mode;
  
  siov[0].addr = name;
  siov[0].size = req.args.mkdir.name_sz;
    
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, &reply, NELEM(siov), siov, 0, NULL);
  
  return sc;
}


/*
 *
 */
int vfs_stat(struct VNode *vnode, struct stat *rstat)
{
  struct SuperBlock *sb;
  iorequest_t req = {0};
  msgiov_t riov[1];
  int sc;

  sb = vnode->superblock;

  req.cmd = CMD_STAT;
  req.args.stat.inode_nr = vnode->inode_nr;
    
  riov[0].addr = rstat;
  riov[0].size = sizeof *rstat;
    
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, NELEM(riov), riov);
  
  return sc;
}


/*
 *
 * TODO: Remove - obsolete- use unlink instead.
 */
int vfs_rmdir(struct VNode *dvnode, struct VNode *vnode, char *name)
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

  // TODO: Need to check reference count see if still active.   and vnode_put()

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

  klog_info("vfs_truncate, size:%u, ino_nr:%u", size, (uint32_t)vnode->inode_nr);

  sb = vnode->superblock;

  req.cmd = CMD_TRUNCATE;
  req.args.truncate.inode_nr = vnode->inode_nr;
  req.args.truncate.size = size;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req,  NULL, 0, NULL, 0, NULL);

  klog_info("vfs_truncate, sc:%d", sc);
  return sc;
}


/*
 *
 * TODO: Needs to update nlink of parent directories?
 */
int vfs_rename(struct VNode *src_dvnode, char *src_name,
               struct VNode *dst_dvnode, char *dst_name)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[2];
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
 *
 * TODO: Needs to update nlink 
 */
int vfs_unlink(struct VNode *dvnode, struct VNode *vnode, char *name)
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

  // TODO: Need to check ref count of vnode and vnode_put()

  return sc;
}


/*
 *
 * TODO: Needs to update nlink
 */
int vfs_link(struct VNode *dvnode, char *name, struct VNode *target_inode)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[1];
  int sc;
  
  sb = dvnode->superblock;

  // TODO: Check if link exists?

  req.cmd = CMD_LINK;
  req.args.link.dir_inode_nr = dvnode->inode_nr;
  req.args.link.name_sz = StrLen(name) + 1;
  req.args.link.target_inode_nr = target_inode->inode_nr;
  
  siov[0].addr = name;
  siov[0].size = req.args.link.name_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, NELEM(siov), siov, 0, NULL);  

  if (sc != 0) {
    return sc;
  }

  return 0;
}


/*
 *
 * TODO: Needs to update nlink
 */
int vfs_symlink(struct VNode *dvnode, char *name, char *link, uid_t uid, uid_t gid, mode_t mode)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t siov[3];
  int sc;
  
  sb = dvnode->superblock;

  req.cmd = CMD_SYMLINK;
  req.args.symlink.dir_inode_nr = dvnode->inode_nr;
  req.args.symlink.name_sz = StrLen(name) + 1;
  req.args.symlink.link_sz = StrLen(link) + 1;
  req.args.symlink.uid = uid;
  req.args.symlink.gid = gid;
  req.args.symlink.mode = mode;

  siov[0].addr = name;
  siov[0].size = req.args.symlink.name_sz;
  siov[1].addr = link;
  siov[1].size = req.args.symlink.link_sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, NELEM(siov), siov, 0, NULL);  

  if (sc != 0) {
    return sc;
  }

  return 0;
}


/*
 *
 */
int vfs_rdlink(struct VNode *vnode, char *buf, size_t sz)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  msgiov_t riov[1];
  int sc;
  
  kassert(buf != NULL);
  kassert(sz > 0);
  
  sb = vnode->superblock;

  req.cmd = CMD_RDLINK;
  req.args.rdlink.inode_nr = vnode->inode_nr;
  req.args.rdlink.buf_sz = sz;

  riov[0].addr = buf;
  riov[0].size = sz;

  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, NELEM(riov), riov);  
	
	buf[sz-1] = '\0';
	
  return sc;
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

  kassert(vnode != NULL);
        
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
    klog_error("vfs_read failed :%d", nbytes_read);
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

  kassert(vnode != NULL);

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


/*
 * TODO: vfs_syncfs
 */
int vfs_syncfs(struct SuperBlock *sb)
{
  iorequest_t req = {0};
  int sc;
  
  req.cmd = CMD_SYNCFS;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);
  return sc;
}


/*
 *
 */
int vfs_fsync(struct VNode *vnode)
{
  iorequest_t req = {0};
  struct SuperBlock *sb;
  int sc;
  
  sb = vnode->superblock;

  req.cmd = CMD_FSYNC;
  req.args.fsync.inode_nr = vnode->inode_nr;
  
  sc = ksendmsg(&sb->msgport, KUCOPY, &req, NULL, 0, NULL, 0, NULL);
  return sc;
}


/*
 *
 */
int vfs_poll(struct VNode *vnode, uint32_t *revents)
{
  *revents = 0;
  return 0;
}

