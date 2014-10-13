/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include "msm_fb.h"

#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include "linux/proc_fs.h"

#include <linux/delay.h>

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <mach/vreg.h>
#include <mach/board_lge.h>
#include <linux/pm_qos_params.h>

#if 1 //def EBI2_TOVIS_TUNING_SET
#include <linux/hrtimer.h>
#endif

#define QVGA_WIDTH        240
#define QVGA_HEIGHT       320

static void *DISP_CMD_PORT;
static void *DISP_DATA_PORT;

#define EBI2_WRITE16C(x, y) outpw(x, y)
#define EBI2_WRITE16D(x, y) outpw(x, y)
#define EBI2_READ16(x) inpw(x)

// sd card file end point must finalize necessory "$"
#define EBI2_TOVIS_TUNING_SET	0 // 0 : normal version, 1 : tuning version

#if EBI2_TOVIS_TUNING_SET
#define EBI2_TOVIS_TUNING_LOG  1
#define EBI2_TOVIS_TUNING_FROM_SDCARD  1 // 0 : /data/ili9486  1: /sdcard/external_sd/ili9486 , /sdcard2/
#define EBI2_TOVIS_FILE_READ 1
#define EBI2_TOVIS_FIRST_BOOTING_TIME 		45000	// 45 sec
#define EBI2_TOVIS_NON_FIRST_BOOTING_TIME 	3000	// 3 sec
static struct work_struct work_file_read;
static struct hrtimer file_read_timer;
#else
#define EBI2_TOVIS_FIRST_BOOTING_TIME 		240000	// 4 min
#define EBI2_TOVIS_NON_FIRST_BOOTING_TIME 	10000	// 10 sec
#endif

#if EBI2_TOVIS_TUNING_SET
struct ebi2_tovis_register_value_pair {
	char register_num;
	char register_value[20];	
};

#define EXT_BUF_SIZE		100
static struct ebi2_tovis_register_value_pair ext_reg_settings[EXT_BUF_SIZE];
static int filesystem_read = 0;
static int size_length = 0;
#endif


#if EBI2_TOVIS_TUNING_SET
#define LOOP_INTERVAL		100
#define IS_NUM(c)			((0x30<=c)&&(c<=0x39))
#define IS_CHAR_C(c)		((0x41<=c)&&(c<=0x46))						// Capital Letter
#define IS_CHAR_S(c)		((0x61<=c)&&(c<=0x66))						// Small Letter
#define IS_VALID(c)			(IS_NUM(c)||IS_CHAR_C(c)||IS_CHAR_S(c))		// NUM or CHAR
#define TO_BE_NUM_OFFSET(c)	(IS_NUM(c) ? 0x30 : (IS_CHAR_C(c) ? 0x37 : 0x57))	
#define TO_BE_READ_SIZE		 EXT_BUF_SIZE*40							// 8pages (4000x8)

static char *file_buf_alloc_pages=NULL;

static long ebi2_tovis_read_ext_reg(char *filename)
{	
	int value=0, read_idx=0, i=0, j=0, k=0;
	struct file *phMscd_Filp = NULL;
	mm_segment_t old_fs=get_fs();
	phMscd_Filp = filp_open(filename, O_RDONLY |O_LARGEFILE, 0);

	printk("%s : enter this function!\n", __func__);

	if (IS_ERR(phMscd_Filp)) {
		printk("%s : open error!\n", __func__);
		return 0;
	}

	file_buf_alloc_pages = kmalloc(TO_BE_READ_SIZE, GFP_KERNEL);	

	if(!file_buf_alloc_pages) {
		printk("%s : mem alloc error!\n", __func__);
		return 0;
	}

	set_fs(get_ds());
	phMscd_Filp->f_op->read(phMscd_Filp, file_buf_alloc_pages, TO_BE_READ_SIZE-1, &phMscd_Filp->f_pos);
	set_fs(old_fs);

	do
	{		
		if(file_buf_alloc_pages[read_idx]=='['){
			if (file_buf_alloc_pages[read_idx]=='[' && file_buf_alloc_pages[read_idx+2]==']'){ // one value
				read_idx += 1;

				value = file_buf_alloc_pages[read_idx]-TO_BE_NUM_OFFSET(file_buf_alloc_pages[read_idx]);
				read_idx += 1;
				ext_reg_settings[i].register_num = value;
#if EBI2_TOVIS_TUNING_LOG
				printk("%s : case 1, i = %d, value = %d\n", __func__, i, value);
#endif
			}
			else if (file_buf_alloc_pages[read_idx]=='[' && file_buf_alloc_pages[read_idx+3]==']'){ // two value
				read_idx += 1;

				value = (file_buf_alloc_pages[read_idx]-TO_BE_NUM_OFFSET(file_buf_alloc_pages[read_idx]))*10 \
						+ (file_buf_alloc_pages[read_idx+1]-TO_BE_NUM_OFFSET(file_buf_alloc_pages[read_idx+1]));

				read_idx += 2;
				ext_reg_settings[i].register_num = value;
#if EBI2_TOVIS_TUNING_LOG
				printk("%s : case 2, i = %d, value = %d\n", __func__, i, value);
#endif
			}



			for(j=0; j < LOOP_INTERVAL; j++){
				if ((file_buf_alloc_pages[read_idx]=='0' && file_buf_alloc_pages[read_idx+1]=='x' 
							&& file_buf_alloc_pages[read_idx + 2] != ' ') || (file_buf_alloc_pages[read_idx]=='0' && file_buf_alloc_pages[read_idx+1]=='X' 
								&& file_buf_alloc_pages[read_idx + 2] != ' ')){	// skip : 0x, 0X
					read_idx += 2;

					value = (file_buf_alloc_pages[read_idx]-TO_BE_NUM_OFFSET(file_buf_alloc_pages[read_idx]))*0x10 \
							+ (file_buf_alloc_pages[read_idx+1]-TO_BE_NUM_OFFSET(file_buf_alloc_pages[read_idx+1]));

					read_idx += 2;
					ext_reg_settings[i].register_value[k++] = value;

#if EBI2_TOVIS_TUNING_LOG
					printk("%s : case 3, value = %x\n", __func__, value);
#endif
				}	
				else
				{
					if(file_buf_alloc_pages[read_idx]=='['){
						read_idx -= 5;
						break;
					}

					read_idx += 1;
				}
			}	
			k = 0;
			read_idx += 1;
			i++;
		}		
		else
		{
			++read_idx;
		}
	}while(file_buf_alloc_pages[read_idx] != '$');

	kfree(file_buf_alloc_pages);
	file_buf_alloc_pages=NULL;
	filp_close(phMscd_Filp,NULL);

	return i;
}

static long ebi2_tovis_reg_init_ext(void)
{	
	uint16_t length = 0;

	printk("%s\n", __func__);
#if EBI2_TOVIS_TUNING_FROM_SDCARD
	length = ebi2_tovis_read_ext_reg("/sdcard/_ExternalSD/ebi2_tovis");
#else
	length = ebi2_tovis_read_ext_reg("/data/ebi2_tovis");
#endif	
	printk("%s : length = %d!\n", __func__, length);	

	if (!length)
		return 0;
	else
		return length;
}
#endif

static boolean disp_initialized = FALSE;
struct msm_fb_panel_data tovis_qvga_panel_data;
struct msm_panel_ilitek_pdata *tovis_qvga_panel_pdata;
struct pm_qos_request_list *tovis_pm_qos_req;

/* For some reason the contrast set at init time is not good. Need to do
 * it again
 */

/*                                                                        */
#if 0 
static boolean display_on = FALSE;
#else
int display_on = FALSE; 
#endif
/*                                                                        */


/*                                                                                               */
#define LCD_RESET_SKIP 1
int IsFirstDisplayOn = LCD_RESET_SKIP; 
/*                                                                                               */

#define DISP_SET_RECT(csp, cep, psp, pep) \
{ \
	EBI2_WRITE16C(DISP_CMD_PORT, 0x2a);			\
	EBI2_WRITE16D(DISP_DATA_PORT,(csp)>>8);		\
	EBI2_WRITE16D(DISP_DATA_PORT,(csp)&0xFF);	\
	EBI2_WRITE16D(DISP_DATA_PORT,(cep)>>8);		\
	EBI2_WRITE16D(DISP_DATA_PORT,(cep)&0xFF);	\
	EBI2_WRITE16C(DISP_CMD_PORT, 0x2b);			\
	EBI2_WRITE16D(DISP_DATA_PORT,(psp)>>8);		\
	EBI2_WRITE16D(DISP_DATA_PORT,(psp)&0xFF);	\
	EBI2_WRITE16D(DISP_DATA_PORT,(pep)>>8);		\
	EBI2_WRITE16D(DISP_DATA_PORT,(pep)&0xFF);	\
}


#if defined(CONFIG_MACH_MSM7X25A_E0EU) || defined(CONFIG_MACH_MSM7X27A_M3MPCS) || defined(CONFIG_MACH_MSM7X27A_TRFUS)
unsigned int mactl = 0x48;
#else
static unsigned int mactl = 0x98;
#endif

#ifdef TUNING_INITCODE
module_param(te_lines, uint, 0644);
module_param(mactl, uint, 0644);
#endif

static void tovis_qvga_disp_init(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	if (disp_initialized)
		return;

	mfd = platform_get_drvdata(pdev);

	DISP_CMD_PORT = mfd->cmd_port;
	DISP_DATA_PORT = mfd->data_port;

	disp_initialized = TRUE;
}

static void msm_fb_ebi2_power_save(int on)
{
	struct msm_panel_ilitek_pdata *pdata = tovis_qvga_panel_pdata;

	if(pdata && pdata->lcd_power_save)
		pdata->lcd_power_save(on);
}

static int ilitek_qvga_disp_off(struct platform_device *pdev)
{

	/*                                                                        */
#if 1
	struct msm_panel_ilitek_pdata *pdata = tovis_qvga_panel_pdata;
#endif
	/*                                                                        */


	printk("%s: display off...", __func__);
	if (!disp_initialized)
		tovis_qvga_disp_init(pdev);

#ifndef CONFIG_ARCH_MSM7X27A
	pm_qos_update_request(tovis_pm_qos_req, PM_QOS_DEFAULT_VALUE);
#endif

	EBI2_WRITE16C(DISP_CMD_PORT, 0x28);
	msleep(50);
	EBI2_WRITE16C(DISP_CMD_PORT, 0x10); // SPLIN
	msleep(120);

	/*                                                                        */
#if 1 
	if(pdata->gpio)
		gpio_set_value(pdata->gpio, 0);
#endif	
	/*                                                                        */

	msm_fb_ebi2_power_save(0);
	display_on = FALSE;

	return 0;
}

static void ilitek_qvga_disp_set_rect(int x, int y, int xres, int yres) // xres = width, yres - height
{
	if (!disp_initialized)
		return;

	DISP_SET_RECT(x, x+xres-1, y, y+yres-1);
	EBI2_WRITE16C(DISP_CMD_PORT,0x2c); // Write memory start
}

static void do_ilitek_init(struct platform_device *pdev)
{
#if defined(CONFIG_MACH_MSM7X25A_E0EU) || defined(CONFIG_MACH_MSM7X27A_M3MPCS) || defined(CONFIG_MACH_MSM7X27A_TRFUS)

	int x,y;

#if EBI2_TOVIS_TUNING_SET
	if(IsFirstDisplayOn==0 && filesystem_read==1)
	{
		int cmd_port_val = 0;
		int data_port_val = 0;
		int j = 0;	
		int loop_num = 0; 
		printk("lcd tuning mode enter\n");
		for(loop_num = 0; loop_num < size_length; loop_num++)
		{

			cmd_port_val = ext_reg_settings[loop_num].register_value[0];
			EBI2_WRITE16C(DISP_CMD_PORT, cmd_port_val);
			for(j = 1; j < 20; j++)
			{
				data_port_val = ext_reg_settings[loop_num].register_value[j];
				EBI2_WRITE16D(DISP_DATA_PORT,data_port_val);	
				if(j == (ext_reg_settings[loop_num].register_num-1))
					break;
			}
		}
	}
	else
	{
		/* EXTC Option*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xcf);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x21);
		EBI2_WRITE16D(DISP_DATA_PORT,0x20);

		/* 3-Gamma Function Off */
		EBI2_WRITE16C(DISP_CMD_PORT, 0xf2);
		EBI2_WRITE16D(DISP_DATA_PORT,0x02);

		/* Inversion Control -> 2Dot inversion*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xb4);
		EBI2_WRITE16D(DISP_DATA_PORT,0x02);

		/* Power control 1 */
		EBI2_WRITE16C(DISP_CMD_PORT, 0xc0);
		EBI2_WRITE16D(DISP_DATA_PORT,0x15);
		EBI2_WRITE16D(DISP_DATA_PORT,0x15);

		/* Power control 2*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xc1);
		EBI2_WRITE16D(DISP_DATA_PORT,0x05);

		/* Power control 3*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xc2);
		EBI2_WRITE16D(DISP_DATA_PORT,0x32);

		/* Vcom control 1*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xc5);
		EBI2_WRITE16D(DISP_DATA_PORT,0xfc);

		/* V-core Setting */
		EBI2_WRITE16C(DISP_CMD_PORT, 0xcb);
		EBI2_WRITE16D(DISP_DATA_PORT,0x31);
		EBI2_WRITE16D(DISP_DATA_PORT,0x24);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x34);

		/* Interface control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xf6);
		EBI2_WRITE16D(DISP_DATA_PORT,0x41);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);

		/* Entry Mode Set*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xb7);
		EBI2_WRITE16D(DISP_DATA_PORT,0x06);

		/* Frame Rate control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xb1);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x1b);

		/* Memory Access control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0x36);
		EBI2_WRITE16D(DISP_DATA_PORT,0x08);

		/* Blanking Porch control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xb5);
		EBI2_WRITE16D(DISP_DATA_PORT,0x02);
		EBI2_WRITE16D(DISP_DATA_PORT,0x02);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x14);

		/* Display Function control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xb6);
		EBI2_WRITE16D(DISP_DATA_PORT,0x02);
		EBI2_WRITE16D(DISP_DATA_PORT,0x82);
		EBI2_WRITE16D(DISP_DATA_PORT,0x27);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);

		/* Pixel Format --> DBI(5 = 16bit) */
		EBI2_WRITE16C(DISP_CMD_PORT, 0x3a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x05);

		/* Tearing Effect Line On */
		EBI2_WRITE16C(DISP_CMD_PORT, 0x35);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);

		/* Tearing effect Control Parameter */
		EBI2_WRITE16C(DISP_CMD_PORT, 0x44);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0xef);

#if 0 //old
		/* Positive Gamma Correction*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x06);
		EBI2_WRITE16D(DISP_DATA_PORT,0x07);
		EBI2_WRITE16D(DISP_DATA_PORT,0x03);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x40);
		EBI2_WRITE16D(DISP_DATA_PORT,0x59);
		EBI2_WRITE16D(DISP_DATA_PORT,0x4d);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
		EBI2_WRITE16D(DISP_DATA_PORT,0x18);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x22);
		EBI2_WRITE16D(DISP_DATA_PORT,0x1d);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);

		/* Negative Gamma Correction*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
		EBI2_WRITE16D(DISP_DATA_PORT,0x06);
		EBI2_WRITE16D(DISP_DATA_PORT,0x23);
		EBI2_WRITE16D(DISP_DATA_PORT,0x24);
		EBI2_WRITE16D(DISP_DATA_PORT,0x01);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x01);
		EBI2_WRITE16D(DISP_DATA_PORT,0x36);
		EBI2_WRITE16D(DISP_DATA_PORT,0x23);
		EBI2_WRITE16D(DISP_DATA_PORT,0x41);
		EBI2_WRITE16D(DISP_DATA_PORT,0x07);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x30);
		EBI2_WRITE16D(DISP_DATA_PORT,0x27);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0e);

#else
		//                                                             
		/* Positive Gamma Correction*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x03);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x10);
		EBI2_WRITE16D(DISP_DATA_PORT,0x14);
		EBI2_WRITE16D(DISP_DATA_PORT,0x09);
		EBI2_WRITE16D(DISP_DATA_PORT,0x4b);
		EBI2_WRITE16D(DISP_DATA_PORT,0xb2);
		EBI2_WRITE16D(DISP_DATA_PORT,0x53);
		EBI2_WRITE16D(DISP_DATA_PORT,0x08);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x1e);
		EBI2_WRITE16D(DISP_DATA_PORT,0x1f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);

		/* Negative Gamma Correction*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
		EBI2_WRITE16D(DISP_DATA_PORT,0x06);
		EBI2_WRITE16D(DISP_DATA_PORT,0x20);
		EBI2_WRITE16D(DISP_DATA_PORT,0x23);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
		EBI2_WRITE16D(DISP_DATA_PORT,0x03);
		EBI2_WRITE16D(DISP_DATA_PORT,0x37);
		EBI2_WRITE16D(DISP_DATA_PORT,0x05);
		EBI2_WRITE16D(DISP_DATA_PORT,0x4d);
		EBI2_WRITE16D(DISP_DATA_PORT,0x06);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
		EBI2_WRITE16D(DISP_DATA_PORT,0x27);
		EBI2_WRITE16D(DISP_DATA_PORT,0x29);
		EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
#endif
		/* Column address*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0x2a); // Set_column_address
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0xef);

		/* Page address*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0x2b); // Set_Page_address
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x00);
		EBI2_WRITE16D(DISP_DATA_PORT,0x01);
		EBI2_WRITE16D(DISP_DATA_PORT,0x3f);

		/* Charge Sharing control*/
		EBI2_WRITE16C(DISP_CMD_PORT, 0xe8); // Charge Sharing Control
		EBI2_WRITE16D(DISP_DATA_PORT,0x84);
		EBI2_WRITE16D(DISP_DATA_PORT,0x1a);
		EBI2_WRITE16D(DISP_DATA_PORT,0x68);

	}	
	/* Exit Sleep - This command should be only used at Power on sequence*/
	EBI2_WRITE16C(DISP_CMD_PORT,0x11); // Exit Sleep	
#else //EBI2_TOVIS_TUNING_SET

	//V0.9.2.1(120719), 2012-07-19
	/* EXTC Option*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xcf);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x21);
	EBI2_WRITE16D(DISP_DATA_PORT,0x20);

	/* 3-Gamma Function Off */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xf2);
	EBI2_WRITE16D(DISP_DATA_PORT,0x02);

	/* Inversion Control -> 2Dot inversion*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xb4);
	EBI2_WRITE16D(DISP_DATA_PORT,0x02);

	/* Power control 1 */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x16);
	EBI2_WRITE16D(DISP_DATA_PORT,0x16);

	/* Power control 2*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x07);

	/* Power control 3*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc2);
	EBI2_WRITE16D(DISP_DATA_PORT,0x43);

	/* Vcom control 1*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc5);
	EBI2_WRITE16D(DISP_DATA_PORT,0xf1);

	/* V-core Setting */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xcb);
	EBI2_WRITE16D(DISP_DATA_PORT,0x31);
	EBI2_WRITE16D(DISP_DATA_PORT,0x24);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x34);

	/* Interface control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xf6);
	EBI2_WRITE16D(DISP_DATA_PORT,0x41);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x30);

	/* Entry Mode Set*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xb7);
	EBI2_WRITE16D(DISP_DATA_PORT,0x06);

	/* Frame Rate control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xb1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1b);

	/* Memory Access control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0x36);
	EBI2_WRITE16D(DISP_DATA_PORT,0x08);

	/* Blanking Porch control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xb5);
	EBI2_WRITE16D(DISP_DATA_PORT,0x02);
	EBI2_WRITE16D(DISP_DATA_PORT,0x02);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x14);

	/* Display Function control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xb6);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x82);
	EBI2_WRITE16D(DISP_DATA_PORT,0x27);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);

	/* Pixel Format --> DBI(5 = 16bit) */
	EBI2_WRITE16C(DISP_CMD_PORT, 0x3a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x05);

	/* Tearing Effect Line On */
	EBI2_WRITE16C(DISP_CMD_PORT, 0x35);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);

	/* Tearing effect Control Parameter */
	EBI2_WRITE16C(DISP_CMD_PORT, 0x44);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0xef);
#if 0 //old
	/* Positive Gamma Correction*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x06);
	EBI2_WRITE16D(DISP_DATA_PORT,0x07);
	EBI2_WRITE16D(DISP_DATA_PORT,0x03);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x40);
	EBI2_WRITE16D(DISP_DATA_PORT,0x59);
	EBI2_WRITE16D(DISP_DATA_PORT,0x4d);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
	EBI2_WRITE16D(DISP_DATA_PORT,0x18);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x22);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1d);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);

	/* Negative Gamma Correction*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x06);
	EBI2_WRITE16D(DISP_DATA_PORT,0x23);
	EBI2_WRITE16D(DISP_DATA_PORT,0x24);
	EBI2_WRITE16D(DISP_DATA_PORT,0x01);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x01);
	EBI2_WRITE16D(DISP_DATA_PORT,0x36);
	EBI2_WRITE16D(DISP_DATA_PORT,0x23);
	EBI2_WRITE16D(DISP_DATA_PORT,0x41);
	EBI2_WRITE16D(DISP_DATA_PORT,0x07);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x30);
	EBI2_WRITE16D(DISP_DATA_PORT,0x27);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0e);

#else
	//                                                           
	/* Positive Gamma Correction*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x03);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x10);
	EBI2_WRITE16D(DISP_DATA_PORT,0x14);
	EBI2_WRITE16D(DISP_DATA_PORT,0x09);
	EBI2_WRITE16D(DISP_DATA_PORT,0x4b);
	EBI2_WRITE16D(DISP_DATA_PORT,0xb2);
	EBI2_WRITE16D(DISP_DATA_PORT,0x53);
	EBI2_WRITE16D(DISP_DATA_PORT,0x08);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1e);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);

	/* Negative Gamma Correction*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x06);
	EBI2_WRITE16D(DISP_DATA_PORT,0x20);
	EBI2_WRITE16D(DISP_DATA_PORT,0x23);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
	EBI2_WRITE16D(DISP_DATA_PORT,0x03);
	EBI2_WRITE16D(DISP_DATA_PORT,0x37);
	EBI2_WRITE16D(DISP_DATA_PORT,0x05);
	EBI2_WRITE16D(DISP_DATA_PORT,0x4d);
	EBI2_WRITE16D(DISP_DATA_PORT,0x06);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0c);
	EBI2_WRITE16D(DISP_DATA_PORT,0x27);
	EBI2_WRITE16D(DISP_DATA_PORT,0x29);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f);
#endif

	/* Column address*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0x2a); // Set_column_address
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0xef);

	/* Page address*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0x2b); // Set_Page_address
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00);
	EBI2_WRITE16D(DISP_DATA_PORT,0x01);
	EBI2_WRITE16D(DISP_DATA_PORT,0x3f);

	/* Charge Sharing control*/
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe8); // Charge Sharing Control
	EBI2_WRITE16D(DISP_DATA_PORT,0x84);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x38);

	/* Exit Sleep - This command should be only used at Power on sequence*/
	EBI2_WRITE16C(DISP_CMD_PORT,0x11); // Exit Sleep
#endif //EBI2_TOVIS_TUNING_SET


	msleep(120);

	/*                                                                        */
#if 1
	EBI2_WRITE16C(DISP_CMD_PORT,0x2c); // Write memory start
	for(y = 0; y < 320; y++) {
		int pixel = 0x0;
		for(x= 0; x < 240; x++) {
			EBI2_WRITE16D(DISP_DATA_PORT,pixel);
		}
	}
	msleep(30);
#endif
	/*                                                                        */


	EBI2_WRITE16C(DISP_CMD_PORT,0x29); // Display On
#else
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x1D); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xc1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x11); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xc5);
	EBI2_WRITE16D(DISP_DATA_PORT,0x33); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x34); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xcb);
	EBI2_WRITE16D(DISP_DATA_PORT,0x39); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x2c); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x34); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x02); // 5

	EBI2_WRITE16C(DISP_CMD_PORT, 0xcf);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x83); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x30); // 3

	EBI2_WRITE16C(DISP_CMD_PORT, 0xe8);
	EBI2_WRITE16D(DISP_DATA_PORT,0x85); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x01); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x78); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xea);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 2

	/* Power on sequence control */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xed);
	EBI2_WRITE16D(DISP_DATA_PORT,0x64); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x03); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x12); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x81); // 4

	/* Pump ratio control */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xf7);
	EBI2_WRITE16D(DISP_DATA_PORT,0x20); // 1

	/* Display mode setting */
	EBI2_WRITE16C(DISP_CMD_PORT, 0x13);

	/* Memory access control */
	EBI2_WRITE16C(DISP_CMD_PORT, 0x36);
	EBI2_WRITE16D(DISP_DATA_PORT,mactl); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0x3a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x05); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x1A); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb4);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb5);
	EBI2_WRITE16D(DISP_DATA_PORT,0x02); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x02); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x14); // 4

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb6);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x82); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x27); // 3
	//EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 4

	EBI2_WRITE16C(DISP_CMD_PORT, 0x35);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0x44); // Tearing effect Control
	EBI2_WRITE16D(DISP_DATA_PORT,te_lines >> 8); // 1 // peter_ported
	EBI2_WRITE16D(DISP_DATA_PORT,te_lines & 0xFF); // 2 // peter_ported

	/* Positive Gamma Correction */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0F); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x2B); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x2A); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x0B); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x11); // 5
	EBI2_WRITE16D(DISP_DATA_PORT,0x0B); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0x57); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0xF2); // 8
	EBI2_WRITE16D(DISP_DATA_PORT,0x45); // 9
	EBI2_WRITE16D(DISP_DATA_PORT,0x0A); // 10
	EBI2_WRITE16D(DISP_DATA_PORT,0x15); // 11
	EBI2_WRITE16D(DISP_DATA_PORT,0x05); // 12
	EBI2_WRITE16D(DISP_DATA_PORT,0x0B); // 13
	EBI2_WRITE16D(DISP_DATA_PORT,0x04); // 14
	EBI2_WRITE16D(DISP_DATA_PORT,0x04); // 15

	/* Negative Gamma Correction */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x18); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x19); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x0F); // 5
	EBI2_WRITE16D(DISP_DATA_PORT,0x04); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0x28); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0x81); // 8
	EBI2_WRITE16D(DISP_DATA_PORT,0x3D); // 9
	EBI2_WRITE16D(DISP_DATA_PORT,0x06); // 10
	EBI2_WRITE16D(DISP_DATA_PORT,0x0B); // 11
	EBI2_WRITE16D(DISP_DATA_PORT,0x0A); // 12
	EBI2_WRITE16D(DISP_DATA_PORT,0x32); // 13
	EBI2_WRITE16D(DISP_DATA_PORT,0x3A); // 14
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f); // 15

	EBI2_WRITE16C(DISP_CMD_PORT,0x2a); // Set_column_address
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0xef); // 4

	EBI2_WRITE16C(DISP_CMD_PORT,0x2b); // Set_Page_address
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x01); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x3f); // 4

	EBI2_WRITE16C(DISP_CMD_PORT,0xe8); // Charge Sharing Control
	EBI2_WRITE16D(DISP_DATA_PORT,0x85); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x01); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x78); // 3

	EBI2_WRITE16C(DISP_CMD_PORT,0x11); // Exit Sleep

	mdelay(120);

	/*-- bootlogo is displayed at oemsbl
	  EBI2_WRITE16C(DISP_CMD_PORT,0x2c); // Write memory start
	  for(y = 0; y < 320; y++) {
	  int pixel = 0x0;
	  for(x= 0; x < 240; x++) {
	  EBI2_WRITE16D(DISP_DATA_PORT,pixel); // 1
	  }
	  }

	  mdelay(50);
	 */
	EBI2_WRITE16C(DISP_CMD_PORT,0x29); // Display On	
#endif
}

static void do_lgd_init(struct platform_device *pdev)
{
	EBI2_WRITE16C(DISP_CMD_PORT, 0x11);

	EBI2_WRITE16C(DISP_CMD_PORT, 0x35);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0x36);
	EBI2_WRITE16D(DISP_DATA_PORT,mactl); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0x3a);
	EBI2_WRITE16D(DISP_DATA_PORT,0x05); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x17); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb4);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xb6);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x02); // 2

	/* Power Control 1 */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x12); // 1

	/* Power Control 2 */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xc1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x11); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xc5);
	EBI2_WRITE16D(DISP_DATA_PORT,0x22); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x33); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xc7);
	EBI2_WRITE16D(DISP_DATA_PORT,0xcf); // 1

	EBI2_WRITE16C(DISP_CMD_PORT, 0xcb);
	EBI2_WRITE16D(DISP_DATA_PORT,0x39); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x2c); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x35); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x06); // 5

	EBI2_WRITE16C(DISP_CMD_PORT, 0xcf);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0xba); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0xb0); // 3

	EBI2_WRITE16C(DISP_CMD_PORT, 0xe8);
	EBI2_WRITE16D(DISP_DATA_PORT,0x89); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x7b); // 3

	EBI2_WRITE16C(DISP_CMD_PORT, 0xea);
	EBI2_WRITE16D(DISP_DATA_PORT,0x10); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 2

	EBI2_WRITE16C(DISP_CMD_PORT, 0xf6);
	EBI2_WRITE16D(DISP_DATA_PORT,0x01); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x30); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 3

	EBI2_WRITE16C(DISP_CMD_PORT, 0xf7);
	EBI2_WRITE16D(DISP_DATA_PORT,0x20); // 1

	/* Gamma Setting(Positive) */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe0);
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x22); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x30); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x13); // 5
	EBI2_WRITE16D(DISP_DATA_PORT,0x0e); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0x50); // 7
	EBI2_WRITE16D(DISP_DATA_PORT,0x55); // 8
	EBI2_WRITE16D(DISP_DATA_PORT,0x2c); // 9
	EBI2_WRITE16D(DISP_DATA_PORT,0x06); // 10
	EBI2_WRITE16D(DISP_DATA_PORT,0x14); // 11
	EBI2_WRITE16D(DISP_DATA_PORT,0x04); // 12
	EBI2_WRITE16D(DISP_DATA_PORT,0x0a); // 13
	EBI2_WRITE16D(DISP_DATA_PORT,0x08); // 14
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 15

	/* Gamma Setting(Negative) */
	EBI2_WRITE16C(DISP_CMD_PORT, 0xe1);
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 1
	EBI2_WRITE16D(DISP_DATA_PORT,0x0e); // 2
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f); // 3
	EBI2_WRITE16D(DISP_DATA_PORT,0x00); // 4
	EBI2_WRITE16D(DISP_DATA_PORT,0x0c); // 5
	EBI2_WRITE16D(DISP_DATA_PORT,0x02); // 6
	EBI2_WRITE16D(DISP_DATA_PORT,0x2e); // 7
	EBI2_WRITE16D(DISP_DATA_PORT,0x25); // 8
	EBI2_WRITE16D(DISP_DATA_PORT,0x49); // 9
	EBI2_WRITE16D(DISP_DATA_PORT,0x05); // 10
	EBI2_WRITE16D(DISP_DATA_PORT,0x0c); // 11
	EBI2_WRITE16D(DISP_DATA_PORT,0x0b); // 12
	EBI2_WRITE16D(DISP_DATA_PORT,0x35); // 13
	EBI2_WRITE16D(DISP_DATA_PORT,0x38); // 14
	EBI2_WRITE16D(DISP_DATA_PORT,0x0f); // 15

	EBI2_WRITE16C(DISP_CMD_PORT, 0x29);
}

/*                                                                        */
extern int mcs8000_ts_on(void);   //build error, need to fix peter_ported
/*                                                                        */


static int ilitek_qvga_disp_on(struct platform_device *pdev)
{
	struct msm_panel_ilitek_pdata *pdata = tovis_qvga_panel_pdata;

	printk("%s: display on...", __func__);
	if (!disp_initialized)
		tovis_qvga_disp_init(pdev);

	if(pdata->initialized && system_state == SYSTEM_BOOTING) {
		/* Do not hw initialize */
	} else {

		/*                                                     
                            */
		mcs8000_ts_on(); /* peter_ported */
		/*                                                     
                            */

		msm_fb_ebi2_power_save(1);

		/*                                                                        */
#if 1
		/*                                                                                               */
		if(IsFirstDisplayOn==0)
		{
			if(pdata->gpio) {
				//mdelay(10);	// prevent stop to listen to music with BT	
				gpio_set_value(pdata->gpio, 1);
				mdelay(1);
				gpio_set_value(pdata->gpio, 0);
				mdelay(10);
				gpio_set_value(pdata->gpio, 1);
				msleep(120);
			}
		}

		if(pdata->maker_id == PANEL_ID_LGDISPLAY)// peter_ported
			do_lgd_init(pdev); //// peter_ported
		else
			do_ilitek_init(pdev);

		if(IsFirstDisplayOn > 0) 
			IsFirstDisplayOn-- ;
		/*                                                                                               */
#endif
		/*                                                                        */
	}

	pm_qos_update_request(tovis_pm_qos_req, 65000);
	display_on = TRUE;	  
	return 0;
}

ssize_t tovis_qvga_show_onoff(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", display_on);
}

ssize_t tovis_qvga_store_onoff(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int onoff;
	struct msm_fb_panel_data *pdata = dev_get_platdata(dev);
	struct platform_device *pd = to_platform_device(dev);

	sscanf(buf, "%d", &onoff);

	if (onoff) {
		pdata->on(pd);
	} else {
		pdata->off(pd);
	}

	return count;
}

DEVICE_ATTR(lcd_onoff, 0664, tovis_qvga_show_onoff, tovis_qvga_store_onoff);

#ifdef EBI2_TOVIS_FILE_READ
static void ebi2_lcd_file_read_start(struct work_struct *work)
{	
#if EBI2_TOVIS_TUNING_SET
	if(filesystem_read == 0){
		size_length = ebi2_tovis_reg_init_ext();
		filesystem_read = 1;
	}
#endif
}

static void file_read_timer_callfunction(void)
{	
	schedule_work(&work_file_read);	
}

static enum hrtimer_restart file_read_timer_func(struct hrtimer *timer)
{	
	file_read_timer_callfunction();	
	return HRTIMER_NORESTART;
}
#endif

static int __init tovis_qvga_probe(struct platform_device *pdev)
{
	int ret;
#ifdef EBI2_TOVIS_FILE_READ
	int value = 0;
#endif

	printk("tovis_qvga_probe\n");
	if (pdev->id == 0) {
		tovis_qvga_panel_pdata = pdev->dev.platform_data;
		printk("tovis_qvga_probe2\n");
		return 0;
	}

#ifdef EBI2_TOVIS_FILE_READ
	INIT_WORK(&work_file_read, ebi2_lcd_file_read_start);
	hrtimer_init(&file_read_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	file_read_timer.function = file_read_timer_func;
#endif

	msm_fb_add_device(pdev);

	ret = device_create_file(&pdev->dev, &dev_attr_lcd_onoff);
	if (ret) {
		printk("tovis_qvga_probe device_creat_file failed!!!\n");
	}

#ifndef CONFIG_ARCH_MSM7X27A
	tovis_pm_qos_req = pm_qos_add_request(PM_QOS_SYSTEM_BUS_FREQ, PM_QOS_DEFAULT_VALUE);
#endif

#ifdef EBI2_TOVIS_FILE_READ
	printk("%s, hrtimer_start..\n",__func__);
	value = EBI2_TOVIS_FIRST_BOOTING_TIME;
	hrtimer_start(&file_read_timer,
			ktime_set(value / 1000, (value % 1000) * 1000000),
			HRTIMER_MODE_REL);
#endif


	return 0;
}

struct msm_fb_panel_data tovis_qvga_panel_data = {
	.on = ilitek_qvga_disp_on,
	.off = ilitek_qvga_disp_off,
	.set_backlight = NULL,
	.set_rect = ilitek_qvga_disp_set_rect,
};

static struct platform_device this_device = {
	.name   = "ebi2_tovis_qvga",
	.id	= 1,
	.dev	= {
		.platform_data = &tovis_qvga_panel_data,
	}
};

static struct platform_driver __refdata this_driver = {
	.probe  = tovis_qvga_probe,
	.driver = {
		.name   = "ebi2_tovis_qvga",
	},
};

static int __init tovis_qvga_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		pinfo = &tovis_qvga_panel_data.panel_info;
		pinfo->xres = QVGA_WIDTH;
		pinfo->yres = QVGA_HEIGHT;
		pinfo->type = EBI2_PANEL;
		pinfo->pdest = DISPLAY_1;
		pinfo->wait_cycle = 0x428000; //                                                                                            

		pinfo->bpp = 16;
#ifdef CONFIG_FB_MSM_TRIPLE_BUFFER
		pinfo->fb_num = 3;
#else
		pinfo->fb_num = 2;
#endif
		pinfo->lcd.vsync_enable = TRUE;
		pinfo->lcd.refx100 = 6000;
		pinfo->lcd.v_back_porch = 0x06;
		pinfo->lcd.v_front_porch = 0x0a;
		pinfo->lcd.v_pulse_width = 2;
		pinfo->lcd.hw_vsync_mode = TRUE;
		pinfo->lcd.vsync_notifier_period = 0;

		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}



module_init(tovis_qvga_init);

