
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

/*
 * Raspberry Pi interrupt handling functions.
 */

//#define KDEBUG

#include <kernel/board/arm.h>
#include <kernel/board/interrupt.h>
#include <kernel/board/timer.h>
#include <kernel/board/globals.h>
#include <kernel/dbg.h>
#include <kernel/error.h>
#include <kernel/globals.h>
#include <kernel/proc.h>
#include <kernel/timer.h>
#include <kernel/types.h>
#include <kernel/utility.h>
#include <kernel/interrupt.h>


/* @brief   Initialize interrupt controller hardware on the Pi 4
 *
 * Disables the distributor and cpu interface prior to initialization
 */
void init_interrupt_controller(void)
{
  Info("init_interrupt_controller()");
  
  hal_mmio_write(&gic_dist_regs->enable, 0);
  hal_mmio_write(&gic_cpu_iface_regs->icontrol, 0);

  init_gicv2_distributor();
  init_gicv2_cpu_iface();
}


/* @brief   Initialize the GIC distributor
 *
 * Only supports 192 interrupts on the Raspberry Pi 4,
 * 256 are reported for gic-400 though not all used.
 *
 * The Raspberry Pi has this GIC400 (GIC v2) interrupt controller
 * and a ARMC legacy interrupt controller.  The GIC is enabled by default
 * by the start4.elf boot code.  The enable_gic=1 option exists in
 * the config.txt file in the boot partition that can override this
 * although we only support the GIC for the Pi 4.
 */
void init_gicv2_distributor(void)
{
  uint32_t type;
  uint32_t cpumask;
//  uint32_t gic_cpus;
  uint32_t nr_lines;

  cpumask = hal_mmio_read(&gic_dist_regs->targets[0]) & 0xFF;
  cpumask |= cpumask << 8;
  cpumask |= cpumask << 16;

  type = hal_mmio_read(&gic_dist_regs->ic_type);
  nr_lines = 32 * ((type & GICD_TYPE_LINES) + 1);  
//  gic_cpus = 1 + ((type & GICD_TYPE_CPUS) >> 5);

  Info("GIC cpumask = %08x", cpumask);
  Info("GIC type = 0x%08x, %d dec", type, type);   
  Info("GIC cpu_if_ident = %08x", hal_mmio_read(&gic_cpu_iface_regs->cpu_if_ident));
  Info("GIC nr_lines: %d, NIRQ: %d", nr_lines, NIRQ);
//  Info("GIC cpus = %d", gic_cpus);  

  KASSERT(nr_lines >= NIRQ);

  /* Disable global interrupts */
  for (int i = 32; i < nr_lines; i += 32 ) {
    hal_mmio_write(&gic_dist_regs->enable_clr[i / 32], 0xFFFFFFFF);
    hal_mmio_write(&gic_dist_regs->pending_clr[i / 32], 0xFFFFFFFF);
  }

  /* Set interrupts to group 0 */
  for (int i = 32; i < NIRQ; i += 32 ) {
    hal_mmio_write(&gic_dist_regs->group[i / 32], 0x00000000);
  }

  /* Default all global IRQs to level, active low */
  for (int i = 32; i < NIRQ; i += 16 ) {
    hal_mmio_write(&gic_dist_regs->config[i / 16], 0x00000000);
  }

  /* Route all global IRQs to this CPU */
  for (int i = 32; i < NIRQ; i += 4 ) {
    hal_mmio_write(&gic_dist_regs->targets[i / 4], 0x01010101); // FIXME: use cpumask
  }
  
  /* Default priority for global interrupts */
  for (int i = 32; i < NIRQ; i += 4 ) {
    hal_mmio_write(&gic_dist_regs->priority[i / 4],
                   GIC_PRI_IRQ << 24 | GIC_PRI_IRQ << 16 | GIC_PRI_IRQ << 8 | GIC_PRI_IRQ);
  }
  
  /* Turn on the distributor */
  hal_mmio_write(&gic_dist_regs->enable, GICD_CTL_ENABLE);
}


/* @brief   Setup per-cpu registers in the distributor and cpu interface
 */
void init_gicv2_cpu_iface(void)
{
  /* GIC Distributor per CPU */    
  hal_mmio_write(&gic_dist_regs->active[0], 0xFFFFFFFF);
  hal_mmio_write(&gic_dist_regs->enable_set[0], 0x0000FFFF);
  hal_mmio_write(&gic_dist_regs->enable_clr[0], 0xFFFF0000);
  
  /* Set SGI priorities */
  for (int i = 0; i < 16; i += 4 ) {
    hal_mmio_write(&gic_dist_regs->priority[i / 4],
                   GIC_PRI_IPI << 24 | GIC_PRI_IPI << 16 | GIC_PRI_IPI << 8 | GIC_PRI_IPI);
  }

  /* Set PPI priorities */
  for (int i = 16; i < 32; i += 4 ) {
    hal_mmio_write(&gic_dist_regs->priority[i / 4],
                   GIC_PRI_IRQ << 24 | GIC_PRI_IRQ << 16 | GIC_PRI_IRQ << 8 | GIC_PRI_IRQ);
  }

  /* GIC CPU interface controller */
  
  /* Do not mask based on interrupt priority */  
  hal_mmio_write(&gic_cpu_iface_regs->pri_msk_c, 0xFF);

  /* Finest granularity of priority */
  hal_mmio_write(&gic_cpu_iface_regs->pb_c, 0x0);

  /* Enable interrupt delivery to cpu interface controller */
  hal_mmio_write(&gic_cpu_iface_regs->icontrol, GICC_CTL_ENABLE);
}


/* @brief   Wrapper around interrupt top half
 *
 * Called from irq_vector in vectors.S.
 *
 * TODO: Move calls to KernelLock into assembler language wrappers.
 * TODO: Make KernelLock a recursive, counting lock for cleaner handling 
 */
void interrupt_handler(struct UserContext *context)
{
  bool unlock = false;

  if (bkl_locked == false) {
    KernelLock();
    unlock = true;
  }

  interrupt_top_half();

  if (unlock == true) {
    KernelUnlock();
    check_signals(context);
  }
}


/*
 *
 */
void interrupt_top_half(void)
{
  uint32_t irq_ack_reg = hal_mmio_read(&gic_cpu_iface_regs->int_ack);
  uint32_t irq = irq_ack_reg & 0x3FF;

  if (irq == IRQ_SPURIOUS) {
    eoi_interrupt(irq_ack_reg);
    return;
  }
  
  if (irq == IRQ_TIMER3) {
    eoi_interrupt(irq_ack_reg);
    interrupt_top_half_timer();
  } else {
    irq_mask_cnt[irq]++;
    disable_irq(irq);

    eoi_interrupt(irq_ack_reg);

    interrupt_server_broadcast_event(irq);
  }
}


/*
 *
 */
void eoi_interrupt(uint32_t irq_ack_reg)
{
 	 hal_mmio_write(&gic_cpu_iface_regs->eoi, irq_ack_reg);
}


/*
 * Unmasks an IRQ line
 */
void enable_irq(int irq)
{
  uint32_t n = irq / 32;
  uint32_t offset = irq % 32;

  hal_mmio_write(&gic_dist_regs->pending_clr[n], (1 << offset));
  hal_mmio_write(&gic_dist_regs->enable_set[n], (1 << offset));
}


/*
 * Masks an IRQ line
 */
void disable_irq(int irq)
{
  uint32_t n = irq / 32;
  uint32_t offset = irq % 32;

  hal_mmio_write(&gic_dist_regs->enable_clr[n], (1 << offset));
  hal_mmio_write(&gic_dist_regs->pending_clr[n], (1 << offset));
}


/*
 *
 */
void set_irq_priority(int irq, int priority)
{
	uint32_t n = irq / 4;
  uint32_t offset = irq % 4;
  uint32_t tmp;

  tmp = hal_mmio_read(&gic_dist_regs->priority[n]);
  tmp &= ~(0xFF << (offset * 8));

  hal_mmio_write(&gic_dist_regs->priority[n], tmp | ((priority & 0xFF) << (offset * 8)));
}


/*
 *
 */
void set_irq_config(int irq, int config)
{
	uint32_t n = irq / 16;
  uint32_t offset = irq % 16;
  uint32_t tmp;

  tmp = hal_mmio_read(&gic_dist_regs->config[n]);
  tmp &= ~(0x3 << (offset * 2));

  hal_mmio_write(&gic_dist_regs->config[n], tmp | ((config & 0x3) << (offset * 2)));
}


#if 0
void print_pending_interrupts(void)
{
  for (uint32_t irq=0; irq < 128; irq+=32) {
    uint32_t n = irq / 32;
    uint32_t reg = hal_mmio_read(&gic_dist_regs->spi[n]);
    Info ("*** Pending interrupts irq:%d-%d : %08x", irq, irq+31, reg);
  }  
}
#endif


