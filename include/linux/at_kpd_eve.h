/** include/asm-arm/arch-msm/msm_rpcrouter.h
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2009, Code Aurora Forum. All rights reserved.
 * Author: San Mehat <san@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#ifndef _AT_KPD_EVE_H
#define _AT_KPD_EVE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/platform_device.h>
//                                      
// 0001840: [ARM9] AT%GKPD / AT%FKPD added 
//#if defined(CONFIG_MACH_MSM7X27_SWIFT)
#if 1
#include <linux/delay.h>
#endif /* CONFIG_MACH_MSM7X27_SWIFT */
//                                    

void vibrator_disable(void);//                               
void vibrator_enable(void);//                               
int is_vib_state(void);
void vibrator_set(int value);

int at_gkpd_cfg( unsigned int type, int value );
void write_gkpd_value( int value );
int at_fkpd_cfg ( unsigned int type, int value);

//                                      
// 0001840: [ARM9] AT%GKPD / AT%FKPD added 
enum {
ATCMD_DUMMY1,	
ATCMD_DUMMY2,
ATCMD_LCD_REPORTKEY,
ATCMD_FKPD_REPORTKEY,
ATCMD_GKPD_REPORTKEY

};
//                                    

#endif
