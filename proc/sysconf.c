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
 * System configuration info
 */

//#define KDEBUG

#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/msg.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/arch.h>
#include <unistd.h>


/* @brief   Get system configuration information
 *
 * @param   name, the name of a setting to get
 * @return  value of the named configuration or negative errno on failure
 */
int sys_sysconf(int name)
{
  int ret;
  
  switch(name) {
    case _SC_PAGE_SIZE:
      ret = PAGE_SIZE;
      break;
   
    case _SC_PROCESS_MAX:
      ret = max_process;
      break;
      
    default:
      ret = -ENOSYS;
      break;
  }
  
  return ret;
}

