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
 
/* @brief   The Trampoline Microkernel
 *
 * Work-In-Progress. Currently Unused, Ignore for now
 *
 * The Trampoline Microkernel is intended to implement multiple protection rings
 * in CheviotOS and reduce the size of the code that runs within the kernel. The
 * intention is for this file to be the only piece of code running in the kernel
 * and common to all address spaces.
 * 
 * The existing "microkernel" will be renamed the "executive". This will be moved
 * from kernel space into its own address space as the most privileged ring. Compared tp
 * a traditional monolithic OS with a 2 ring system we would effectively be rotating
 * the user-kernel separation 90 degrees with this code allowing a thread to leap-frog
 * from this address space into the kernel's address space.
 *
 * The Trampoline Microkernel is effectively Kernel Page Table Isolation (KPTI) but
 * with additional address spaces for different protection rings. Instead of staying in 
 * the kernel as KPTI does, we drop back into user mode (or sys mode if we are the most
 * privileged ring). There is no scheduler or concept of processes or multiple threads
 * within the Trampoline Microkernel. It is effectively the "Task State Segment" (TSS) on
 * x86 processors but implemented in software with each ring in its own address space.
 *
 * The most privileged ring is then responsible for scheduling, process and thread management.
 * As such this ring should run with supervisor privileges in order to enable and disable
 * interrupts and perform other privileged instructions. This most privileged ring we name
 * the executive.  The intention is for the current kernel to become this executive, running
 * in its own address space.  At a later date the filesystem code within the kernel could
 * be split out into its own ring.
 *
 * Whatever runs in the most privileged ring, if it has supervisor access or access to 
 * physical memory it would still be part of the trusted computing base.
 *
 * Not all rings may be used. For unused rings the thread should be denied access.
 * No stacks need be allocated for the thread in unused rings.
 *
 * A useful property of a Trampoline Microkernel is that there is no scheduling involved
 * in crossing rings and if there is little or no contention for locks in the ring above
 * this can avoid having to call into the executive to block on locks, avoiding a lot of
 * scheduling decisions.
 * 
 * It is expected that the rings can implement services such as the GUI and audio servers
 * perhaps even the network server or protected libraries. These have a passive interface.
 * Graphics and audio drivers could call into these rings, bypassing the filesystem
 * iorequest_t messaging that currently exists.
 *
 * A number of existing microkernels and OSes have attempted to implement a more general
 * cross domain call mechanism. This has been known as Migrating Threads, Lightweight-RPC
 * (LRPC), Thread Tunnelling, Synchronous IPC and Passive Object Model Microkernels.
 *
 * A mechanism is needed to indicate an intermediate ring is calling a more privileged
 * ring on behalf of a less privileged ring. This is to be used for implementing a reference
 * monitor. For example calls to open() are redirected to the intermediate reference monitor
 * that performs additional checks.  This then calls open() in the executive but indicates
 * the file descriptor should be assigned to the original ring. Sun's Pebble Component Based OS
 * had a mechanism with similar intent for call interposition.
 *
 *
 * struct CPU {
 *   void *stack;
 *   int cpl;                       // current privilege level/ring of the thread.
 *                                  // pl0 (ring 0) is the most privileged ring.
 *                                  // Numerically higher privilege levels are less privileged.
 *   struct {
 *     uint32_t access_bitmap;      // Bitmap of rings that can access this ring
 *                                  // We could use 2 integers instead for min/max bracketing.
 *                                  // We shouldn't allow downcalls so more privileged rings
 *                                  // should be cleared in the bitmap.
 *                                  // We could allow downcalls but would need to adjust
 *                                  // the entry stack pointer 
 *     void *pagedir;               // page directory of this ring
 *
 *     void *entry_pc;              // program counter entry point into this ring
 *     void *entry_sp;              // top of stack pointer for this thread in this ring 
 *     uint32_t entry_flags;        // cpu mode (includes interrupt flag) to enter into.
 *                                  // The executive (the most privileged ring) should
 *                                  // run with SYS or supervisor privileges. 
 *
 *     int ret_pl;                  // Privilege level to return to from this ring for this thread
 *                                  // This forms a chain down to the lowest privilege level.
 *     void *ret_pc;                // The next instruction to return to
 *     void *ret_sp;                // The stack pointer to return to
 *     uint32_t ret_flags;          // cpu mode (includes interrupt flag) to return into
 *                                  // The page directory is obtained from the activation
 *                                  // we are returning to. We could store a copy here.
 *
 *     // Only a small number of parameters are passed in registers, similar to current
 *     // syscalls that have a maximum of 6 parameters.  Data structures and more than
 *     // 6 parameters must be passed in iov structs.
 *     //
 *     // Transfer of data in IOVs is not done by the trampoline.  This must be done
 *     // by the most privileged ring (the real kernel) which will implement copyin,
 *     // copyout and copyoutstring.
 *
 *     iov_t *iov_in;               // Data that is passed in call to this ring
 *     size_t iov_in_sz;            // Number of iovs 
 *     iov_t *iov_out;              // Data to be returned to the calling ring
 *     size_t iov_out_sz;           // Number of iovs
 *   } activation[MAX_PL];
 * };
 *
 *
 * TODO: Work out register usage
 * 
 * r0 - r5   - 6 standard parameters max (unused parameters must be set 0)
 *             Or use extra register for counter of parameters in registers
 * r6        - syscall number + ring  (16-bit syscall, 16 bit desired privilege level (dpl) of ring to call into
 * r7 - r10  - iov_in, iov_in_sz, iov_out, iov_out_sz
 * lr
 * pc
 * sp
 *
 * Pushed onto stack upon entry into new ring
 * r6       - syscall number (with desired privilege level removed from upper 16 bits).
 *          - calling privilege level - (possibly with a flag to indicate calling on behalf of lower ring).
 * r0 - r5  - syscall arguments
 * 
 */
 
 
/*
 * CPU and Activation structure field offsets
 */
.equ NUM_PL,    6     // Number of privilege rings/activations/address spaces
.equ NUM_CPU,   1     // Number of CPUs

.equ CPU_STACK,                 0
.equ CPU_CPL,                   4
.equ CPU_ACTIVATION,            8

.equ ACT_ACCESS_BITMAP          0
.equ ACT_PAGEDIR                4
.equ ACT_ENTRY_IP               8
.equ ACT_ENTRY_SP               12
.equ ACT_ENTRY_FLAGS            16
.equ CPU_RET_PL,                20
.equ ACT_RET_IP                 24
.equ ACT_RET_SP                 28
.equ ACT_RET_FLAGS              32
.equ ACT_IOV_IN                 36
.equ ACT_IOV_IN_SZ              40
.equ ACT_IOV_OUT                44
.equ ACT_IOV_OUT_SZ             48
.equ SIZEOF_ACTIVATION,         52    

// TODO: Align this up to cache line size so that 2 CPUs don't share the same cache lines.
.equ SIZEOF_CPU,                (CPU_ACTIVATION + (NUM_PL * SIZEOF_ACTIVATION))


/*
 * Data section
 */
.data

// struct CPU cpu_table[NUM_CPU];
.balign 4096
cpu_table:
.skip NUM_CPU * SIZEOF_CPU

// Stacks
.balign 64
trampoline_stack:
.skip NUM_CPU * STACK_SZ
trampoline_stack_top:


/*
 * Text section
 */
.text

/* @brief   System Call into a higher privileged ring
 *
 */
sys_call:
    MSWIPushRegs // Save some registers if needed on kernel stack
    
    // TODO Call into higher ring
    // extract desired ring from register R6
    // check access_bitmap of activation[r6], is 1<<cpl bit set so we can access it

    // If fail, call into most privileged ring as an exception, indicate access permission error.

    // Save iov registers into current activation.    
    // adjust cpl to the new privilege level activation
    // Save return pc, sp and cpu flags into new activation
    // load new pagedir from new activation
    // update user entry point pc and top of stack sp register before returning into new ring
    MSWIPopRegs    
    subs pc, lr, #0   // Return into new ring


/* @brief   System Call to return to the previous ring
 *
 */
sys_return:

    MSWIPushRegs  // Save some registers if needed on kernel stack
    
    // Get returm privilege level from the current activation
    // Set cpl to the return privilege level
    // Load pagedir register with pagedir pointer from returning privilege ring
    // Clean any unsed registers before returning to calling ring
    // Update the user mode return pc and sp to that saved in the current activation    
    MSWIPopRegs    
    subs pc, lr, #0   // Return into previous ring



/* @brief   Entry point for interrupts
 */
.balign 16
irq_vector:
    sub lr, #4
    MExceptionPushRegs
    
    // Call into interrupt handler in most privileged ring, similar to sys_call

    MExceptionPopRegs
    subs pc, lr, #0


/* @brief   Entry point for undefined instruction exceptions
 */
.balign 16
undef_instr_vector:
    MExceptionPushRegs

    // Call into exception handler in most privileged ring, similar to sys_call
    
    MExceptionPopRegs
    subs pc, lr, #0
    

/* @brief   Entry point into kernel for instruction prefetch exceptions
 */
.balign 16
prefetch_abort_vector:
    sub lr, #4
    MExceptionPushRegs

    // Call into exception handler in most privileged ring, similar to sys_call

    MExceptionPopRegs
    subs pc, lr, #0

    
/* @brief   Entry point for exceptions caused by failed reads or writes to memory
*/
.balign 16
data_abort_vector:
    sub lr, #8
    MExceptionPushRegs

    // Call into exception handler in most privileged ring, similar to sys_call

    MExceptionPopRegs   
    subs pc, lr, #0
        
.balign 16
reset_vector:
    bl reset_handler
    bl reset_vector

.balign 16    
reserved_vector:
    bl reserved_handler    
    bl reserved_vector

.balign 16
fiq_vector:
    bl fiq_handler
    bl fiq_vector



/* @brief   Initialize the Trampoline Microkernel
 *
 * Initialize the trampoline and return a pointer to the CPU tables so
 * that the executive can save and reload state on switching threads.
 * Executive can then set a supervisor or sys register to point to
 * the CPU structure of the current process or indirectly through its
 * own CPU structres.
 */
init_trampoline:
    subs pc, lr, #0  


