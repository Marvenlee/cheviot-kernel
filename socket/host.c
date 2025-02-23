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
 */

//#define KDEBUG

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <kernel/dbg.h>
#include <kernel/filesystem.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <kernel/socket.h>
#include <string.h>
#include <netdb.h>



/*
 *
 */

int sys_gethostbyname(const char *name, struct hostent *result)
{
  // name_sz
  
  // reply hostent struct
   
  return -ENOSYS;
}


/*
 *
 */
int sys_gethostbyaddr(const void *addr, socklen_t len, int type, struct hostent *result)
{
  // len
  // type
  // addr as payload
  
  // reply hostent

  return -ENOSYS;
}


