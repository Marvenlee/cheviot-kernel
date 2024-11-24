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
 * User and Group ID management
 */

//#define KDEBUG

#include <sys/types.h>
#include <kernel/proc.h>
#include <kernel/dbg.h>
#include <unistd.h>


/* @brief   Get the real user ID of the calling process
 *
 */
uid_t sys_getuid(void)
{
  struct Process *current = get_current_process();

  return current->uid;    
}


/* @brief   Get the real group ID of the calling process
 *
 */
gid_t sys_getgid(void)
{
  struct Process *current = get_current_process();

  return current->gid;    
}


/* @brief   Get the effective user ID of the calling process
 *
 */
uid_t sys_geteuid(void)
{
  struct Process *current = get_current_process();

  return current->euid;    
}


/* @brief   Get the effective group ID of the calling process
 *
 */
gid_t sys_getegid(void)
{
  struct Process *current = get_current_process();

  return current->egid;    
}


/* @brief   Set the real user ID of the calling process
 *
 */
int sys_setuid(uid_t uid)
{
  struct Process *current = get_current_process();

  if (uid < 0 || uid > UID_MAX) {
    return -EINVAL;
  }

  if (current->uid != uid || current->euid != SUPERUSER) {
    return -EPERM;
  }
    
  current->uid = uid;
  current->euid = uid;
  current->suid = uid;
  return 0;
}


/* @brief   Set the real group ID of the calling process
 *
 */   
int sys_setgid(gid_t gid)
{
  struct Process *current = get_current_process();

  if (gid < 0 || gid > GID_MAX) {
    return -EINVAL;
  }

  if (current->gid != gid || current->euid != SUPERUSER) {
    return -EPERM;
  }
  
  current->gid = gid;
  current->egid = gid;
  current->sgid = gid;
  return 0;    
}


/* @brief   Set the effective user ID of the calling process
 *
 */
int sys_seteuid(uid_t uid)
{
  struct Process *current = get_current_process();

  if (uid < 0 || uid > UID_MAX) {
    return -EINVAL;
  }

  if ((current->uid != uid && current->suid != uid) || current->euid != SUPERUSER) {
    return -EPERM;
  }
  
  current->euid = uid;
	return 0;
}


/* @brief   Set the effective group ID of the calling process
 *
 */
int sys_setegid(gid_t gid)
{
  struct Process *current = get_current_process();

  if (gid < 0 || gid > GID_MAX) {
    return -EINVAL;
  }

  if ((current->gid != gid && current->sgid != gid) || current->euid != SUPERUSER) {
    return -EPERM;
  }
  
  current->egid = gid;
	return 0;
}


/*
 *
 */
int sys_issetugid(void)
{
	return 0;
}


/*
 *
 */
int setreuid(uid_t ruid, uid_t euid)
{
  struct Process *current = get_current_process();

  if (ruid < 0 || euid < 0) {
    return -EINVAL;
  }


  if (current->euid == SUPERUSER ||
      ((ruid == current->uid || ruid == current->euid) &&
      (euid == current->uid || euid == current->euid))) {

    if (ruid != -1) {
      current->uid = ruid;
    }
    
    if (euid != -1) {
      current->euid = euid;
    }

    return 0;
  } else {
    return -EPERM;
  }
}


/*
 *
 */
int setregid(gid_t rgid, uid_t egid)
{
  struct Process *current = get_current_process();

  if (current->euid == SUPERUSER || 
      ((rgid == current->gid || rgid == current->egid) &&
      (egid == current->gid || egid == current->egid))) {

    if (rgid != -1) {
      current->gid = rgid;
    }
    
    if (egid != -1) {
      current->egid = egid;
    }

    return 0;
  } else {
    return -EPERM;
  }
}


/*
 *
 */
int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
  struct Process *current = get_current_process();

  if (current->euid == SUPERUSER ||
      ((ruid == current->uid || ruid == current->euid || ruid == current->suid) &&
       (euid == current->uid || euid == current->euid || euid == current->suid) &&
       (suid == current->uid || suid == current->euid || suid == current->suid))) {

    if (ruid != -1) {
      current->uid = ruid;
    }
    
    if (euid != -1) {
      current->euid = euid;
    }

    if (suid != -1) {
      current->suid = suid;
    }

    return 0;
  } else {
    return -EPERM;
  }
}  


/*
 *
 */
int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
  struct Process *current = get_current_process();

  if (current->euid == SUPERUSER || 
      ((rgid == current->gid || rgid == current->egid || rgid == current->sgid) &&
       (egid == current->gid || egid == current->egid || egid == current->sgid) &&
       (sgid == current->gid || sgid == current->egid || sgid == current->sgid))) {

    if (rgid != -1) {
      current->gid = rgid;
    }
    
    if (egid != -1) {
      current->egid = egid;
    }
    
    if (sgid != -1) {
      current->sgid = sgid;
    }
    
    return 0;
  } else {
    return -EPERM;
  }
}


/*
 *
 */
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
  struct Process *current = get_current_process();

  if (ruid != NULL) {
    CopyOut(ruid, &current->uid, sizeof *ruid);
  }

  if (euid != NULL) {
    CopyOut(euid, &current->euid, sizeof *euid);
  }

  if (suid != NULL) {
    CopyOut(suid, &current->suid, sizeof *suid);
  }

  return 0;
}


/*
 *
 */
int getresgid(uid_t *rgid, uid_t *egid, uid_t *sgid)
{
  struct Process *current = get_current_process();

  if (rgid != NULL) {
    CopyOut(rgid, &current->gid, sizeof *rgid);
  }

  if (egid != NULL) {
    CopyOut(egid, &current->egid, sizeof *egid);
  }

  if (sgid != NULL) {
    CopyOut(sgid, &current->sgid, sizeof *sgid);
  }

  return 0;
}
  
  
/* @brief   Set the supplementary group IDs of the the calling process
 *
 */
int sys_setgroups(int ngroups, const gid_t *grouplist)
{
  struct Process *current = get_current_process();
  int sc;
  
  if (ngroups < 0 || ngroups > NGROUPS_MAX) {
    return -EINVAL;
  }
  
  if (ngroups > 0 && grouplist == NULL) {
    return -EFAULT;
  }

  current->ngroups = 0;

  if (ngroups > 0) {
    sc = CopyIn(current->groups, grouplist, ngroups * sizeof(gid_t));

    if (sc != 0) {
      return -EFAULT;
    }
  }
  	
  for(int i = 0; i < ngroups; i++) {
    if (current->groups[i] < 0 || current->groups[i] > GID_MAX) {
      return -EINVAL;
    }
  }

  for(int i = ngroups; i < NGROUPS_MAX; i++) {
    current->groups[i] = 0;
  }
  
  current->ngroups = ngroups;

  return 0;
}


/* @brief   Get the supplementary group IDs of the the calling process
 *
 */
int sys_getgroups(int gidsetsize, gid_t *grouplist)
{
  struct Process *current = get_current_process();
  int sc;
  
  if (gidsetsize < 0 || gidsetsize > NGROUPS_MAX) {
	  return -EINVAL;
  }
  
  if (gidsetsize == 0) {
    return current->ngroups;
  }
  
  if (gidsetsize < current->ngroups) {
    return -EINVAL;
  }

  sc = CopyOut(grouplist, current->groups, gidsetsize * sizeof(gid_t));  

  if (sc != 0) {
    return -EFAULT;
  }
  
	return current->ngroups;
}


/* @brief   Initialize root process's and kernel tasks' user and group IDs
 *
 */
void init_ids(struct Process *proc)
{
  proc->ngroups = 0;
  
  for(int t=0; t<NGROUPS; t++) {
    proc->groups[t] = 0;
  }
  
  proc->uid = 0;                 // real uid
  proc->gid = 0;                 // real gid
  proc->euid = 0;                // effective uid
  proc->egid = 0;                // effective gid
  proc->suid = 0;                // saved uid
  proc->sgid = 0;                // saved gid  
}


/* @brief   Copy the user and group IDs to a forked process.
 *
 */
void fork_ids(struct Process *new_proc, struct Process *old_proc)
{
  new_proc->ngroups = old_proc->ngroups;
  
  for(int t=0; t<NGROUPS; t++) {
    new_proc->groups[t] = old_proc->groups[t];
  }
  
  new_proc->uid  = old_proc->uid;          // real uid
  new_proc->gid  = old_proc->gid;          // real gid
  new_proc->euid = old_proc->euid;         // effective uid
  new_proc->egid = old_proc->egid;         // effective gid
  new_proc->suid = old_proc->suid;         // saved uid
  new_proc->sgid = old_proc->sgid;         // saved gid
}


