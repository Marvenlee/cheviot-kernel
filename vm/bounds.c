/*
 * Copyright 2014  Marven Gilhespie
 *
 * Licensed under the Apache License, segment_id 2.0 (the "License");
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
 * Memory bounds checking functions
 */

//#define KDEBUG 1

#include <kernel/arch.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/lists.h>
#include <kernel/proc.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/vm.h>
#include <string.h>
#include <sys/mman.h>


/*
 *
 */
int bounds_check(void *addr, size_t sz)
{
  if ((ssize_t)sz < 0) {
    return -E2BIG;
  }

  if ((uintptr_t)addr < VM_USER_BASE || (uintptr_t)addr >= VM_USER_CEILING || 
      sz >= (VM_USER_CEILING - (uintptr_t)addr)) {
    return -EFAULT;
  }
  
  return 0;
}

/*
 *
 */
int bounds_check_kernel(void *addr, size_t sz)
{
  if ((ssize_t)sz < 0) {
    return -E2BIG;
  }

  if ((uintptr_t)addr < VM_KERNEL_BASE || (uintptr_t)addr >= VM_KERNEL_CEILING ||
      sz >= (VM_KERNEL_CEILING - (uintptr_t)addr)) {
    return -EFAULT;
  }
  
  return 0;
}

