/*
 * Copyright 2023  Marven Gilhespie
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
 */

#ifndef MACHINE_BOARD_RASPBERRY_PI_4_PERIPH_BASE_H
#define MACHINE_BOARD_RASPBERRY_PI_4_PERIPH_BASE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * ARM Physical base addresses of peripherals in low peripheral address mode
 */ 
#define PERIPHERAL_BASE       0xFE000000                        /* Peripheral ARM phyiscal address */
#define TIMER_BASE            (PERIPHERAL_BASE + 0x00003000)    /* System Timer */
#define GPIO_BASE             (PERIPHERAL_BASE + 0x00200000)    /* GPIO interface */
#define AUX_BASE              (PERIPHERAL_BASE + 0x00215000)    /* Aux mini-uart and SPI */

#define MBOX_BASE             (PERIPHERAL_BASE + 0x0000B880)    /* VideoCore Mailbox base */

#define GIC400_BASE           0xFF840000
#define GICD_BASE             (GIC400_BASE+0x00001000)          // + 4K ?
#define GICC_BASE             (GIC400_BASE+0x00002000)          // + 8K ?


#define ARM_LOCAL_BASE        0xFF800000

#define EMMC2_BASE            0xFE340000                        /* eMMC/SD-Card registers */



#endif


