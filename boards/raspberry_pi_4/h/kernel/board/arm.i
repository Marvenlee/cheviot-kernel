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

// @brief CPU Modes
.equ MODE_MASK, 0x1f

.equ USR_MODE,  0x10
.equ FIQ_MODE,  0x11
.equ IRQ_MODE,  0x12
.equ SVC_MODE,  0x13
.equ ABT_MODE,  0x17
.equ UND_MODE,  0x1b
.equ SYS_MODE,  0x1f

// @brief Flag register bits
.equ I_BIT,     (1<<7)
.equ F_BIT,     (1<<6)


// @brief   Per CPU structure
.equ MAX_CPU,               8

