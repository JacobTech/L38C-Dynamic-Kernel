/* drivers/video/backlight/rt9396_bl.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <mach/board_lge.h>

#define MODULE_NAME  "rt9396bl"
#define CONFIG_BACKLIGHT_LEDS_CLASS

#ifdef CONFIG_BACKLIGHT_LEDS_CLASS
#include <linux/leds.h>
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/*                                                                              */
#include <linux/notifier.h> 
/*                                                                              */

/********************************************
 * Definition
 ********************************************/
#define LCD_LED_MAX 		0x3f
#define LCD_LED_MIN 		0

#define RT9396_LDO_NUM 	4

#define MAX_VALUE 		255
#define DEFAULT_VALUE 		144 //144->50%, 102->40%
#define MIN_VALUE 		25

#define RT9396BL_MIN_BRIGHTNESS	0x05 
#define RT9396BL_DEFAULT_BRIGHTNESS 	0x19 //for 10.16mA
#define RT9396BL_MAX_BRIGHTNESS 	0x3f

//register
#define RT9396BL_REG_CURRENT	  	0x47   	/* Register address to control Backlight Level */
#define RT9396BL_REG_LED_ON  	  	0x47 	/* Register address for LED on seting */
#define RT9396BL_REG_LED_OFF 	  	0x40 	/* Register address for LED on seting */

//LDO ON
#define RT9396BL_REG_LDO1_ON      	0x23	/* Register address for LDO 1 voltage setting */
#define RT9396BL_REG_LDO2_ON  	  	0x25	/* Register address for LDO 2 voltage setting */
#define RT9396BL_REG_LDO3_ON      	0x29	/* Register address for LDO 3 voltage setting */
#define RT9396BL_REG_LDO4_ON  	  	0x31	/* Register address for LDO 4 voltage setting */
//LDO OFF
#define RT9396BL_REG_LDO1_OFF     	0x22	/* Register address for LDO 1 voltage setting */
#define RT9396BL_REG_LDO2_OFF  	0x24	/* Register address for LDO 2 voltage setting */
#define RT9396BL_REG_LDO3_OFF     	0x28	/* Register address for LDO 3 voltage setting */
#define RT9396BL_REG_LDO4_OFF  	0x30	/* Register address for LDO 4 voltage setting */

//value
#define RT9396BL_VAL_LDO1	      	0x0f	/* value for LDO 1 voltage setting , VDO1 Proxy 2.8v->0x0b 3.3v->0x0f, 12-08-08*/
#define RT9396BL_VAL_LDO2  	  	0x0b	/* value for LDO 2 voltage setting */
#define RT9396BL_VAL_LDO3	      	0x01	/* value for LDO 3 voltage setting */
#define RT9396BL_VAL_LDO4  	  	0x04	/* value for LDO 4 voltage setting */
#define RT9396BL_VAL_LED_SET  	0x34	/* value for LED initial data (not average data) */

#ifdef CONFIG_BACKLIGHT_LEDS_CLASS
#define LEDS_BACKLIGHT_NAME "lcd-backlight"
#endif

enum {
	ALC_MODE,
	NORMAL_MODE,
} RT9396BL_MODE;

enum {
	UNINIT_STATE=-1,
	POWERON_STATE,
	NORMAL_STATE,
	SLEEP_STATE,
	POWEROFF_STATE
} RT9396BL_STATE;

#define dprintk(fmt, args...)   printk(KERN_INFO "%s:%s: " fmt, MODULE_NAME, __func__, ## args);
#define eprintk(fmt, args...)   printk(KERN_ERR "%s:%s: " fmt, MODULE_NAME, __func__, ## args)

struct ldo_vout_struct {
	unsigned char reg;
	unsigned vol;
};

struct rt9396_ctrl_tbl {
	unsigned char reg;
	unsigned char val;
};

struct rt9396_reg_addrs {
	unsigned char ldo_1;	
	unsigned char ldo_2;	
	unsigned char ldo_3;	
	unsigned char ldo_4;	
	unsigned char led_set_on;
	unsigned char led_set_off;
};

struct rt9396_cmds {
	struct rt9396_ctrl_tbl *normal;
	struct rt9396_ctrl_tbl *sleep;
};

struct rt9396_driver_data {
	struct i2c_client *client;
	struct backlight_device *bd;
	struct led_classdev *led;
	int gpio;
	int intensity;
	int max_intensity;
	int mode;
	int state;
	int ldo_ref[RT9396_LDO_NUM];
	unsigned char reg_ldo_enable;
	unsigned char reg_ldo_vout[2];
	int version;
	struct rt9396_cmds cmds;
	struct rt9396_reg_addrs reg_addrs;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

/********************************************
 * Global variable
 ********************************************/
static struct rt9396_driver_data *rt9396_ref;
static int rt9396_powerstate = POWERON_STATE;

/* Set to initial mode */
static struct rt9396_ctrl_tbl rt9396bl_inital_tbl[] = {
	{ RT9396BL_REG_LED_ON, RT9396BL_VAL_LED_SET },   	// LED SET
	{ 0xFF, 0xFE }						//end of table (not erase)
};

/* Set to sleep mode  */
static struct rt9396_ctrl_tbl rt9396bl_sleep_tbl[] = {
	{ RT9396BL_REG_LED_OFF, 0x00 },   	     		// LED SET
	{ 0xFF, 0xFE }   					//end of table (not erase)
};
static int bl_chargerlogo = 0;

/********************************************
 * Functions
 ********************************************/
static int rt9396_setup_version(struct rt9396_driver_data *drvdata)
{
	if(!drvdata)
		return -ENODEV;

		drvdata->cmds.normal = rt9396bl_inital_tbl;
		drvdata->cmds.sleep = rt9396bl_sleep_tbl;
		drvdata->reg_addrs.ldo_1 = RT9396BL_REG_LDO1_OFF;
		drvdata->reg_addrs.ldo_2 = RT9396BL_REG_LDO2_OFF;
		drvdata->reg_addrs.ldo_3 = RT9396BL_REG_LDO3_OFF;
		drvdata->reg_addrs.ldo_4 = RT9396BL_REG_LDO4_OFF;		
		drvdata->reg_addrs.led_set_on = RT9396BL_REG_LED_ON;
		drvdata->reg_addrs.led_set_off = RT9396BL_REG_LED_OFF;

	return 0;
}

static int rt9396_read(struct i2c_client *client, u8 reg, u8 *pval)
{
	int ret;
	int status = 0;

	if (client == NULL) {
		eprintk("client is null\n");
		return -1;
	}

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		status = -EIO;
		eprintk("fail to read(reg=0x%x,val=0x%x)\n", reg,*pval);
	}

	*pval = ret;
	return status;
}

static int rt9396_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	int status = 0;

	if (client == NULL) {
		eprintk("client is null\n");
		return -1;
	}	
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret != 0) {
		status = -EIO;
		eprintk("fail to write(reg=0x%x,val=0x%x)\n", reg, val);
	}

	return status;
}

static int rt9396_set_ldos(struct i2c_client *i2c_dev, unsigned num, int enable)
{
	int err = 0;
	struct rt9396_driver_data *drvdata = i2c_get_clientdata(i2c_dev);
	printk("%s, num:%d, enable %d\n", __func__, num, enable);

	if (drvdata) {
		if (enable) 
			{
			switch(num)
				{
				case 1 :
					drvdata->reg_addrs.ldo_1 = RT9396BL_REG_LDO1_ON;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_1, RT9396BL_VAL_LDO1);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_1, RT9396BL_VAL_LDO1,err);
					break;
				case 2 :
					drvdata->reg_addrs.ldo_2 = RT9396BL_REG_LDO2_ON;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_2, RT9396BL_VAL_LDO2);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_2, RT9396BL_VAL_LDO2,err);
					break;
				case 3 :
					drvdata->reg_addrs.ldo_3 = RT9396BL_REG_LDO3_ON;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_3, RT9396BL_VAL_LDO3);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_3, RT9396BL_VAL_LDO3,err);
					break;
				case 4 :
					drvdata->reg_addrs.ldo_4 = RT9396BL_REG_LDO4_ON;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_4, RT9396BL_VAL_LDO4);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_4, RT9396BL_VAL_LDO4,err);
					break;
				default:
					break;
				}
		
			}
		else {
			switch(num)
				{
				case 1 :
					drvdata->reg_addrs.ldo_1 = RT9396BL_REG_LDO1_OFF;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_1, RT9396BL_VAL_LDO1);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_1, RT9396BL_VAL_LDO1,err);
					break;
				case 2 :
					drvdata->reg_addrs.ldo_2 = RT9396BL_REG_LDO2_OFF;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_2, RT9396BL_VAL_LDO2);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_2, RT9396BL_VAL_LDO2,err);
					break;
				case 3 :
					drvdata->reg_addrs.ldo_3 = RT9396BL_REG_LDO3_OFF;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_3, RT9396BL_VAL_LDO3);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_3, RT9396BL_VAL_LDO3,err);
					break;
				case 4 :
					drvdata->reg_addrs.ldo_4 = RT9396BL_REG_LDO4_OFF;
					err = rt9396_write(i2c_dev, drvdata->reg_addrs.ldo_4, RT9396BL_VAL_LDO4);
					//printk("%s, reg:0x%x val:0x%x err:%d\n", __func__,drvdata->reg_addrs.ldo_4, RT9396BL_VAL_LDO4,err);
					break;
				default:
					break;
				}
		}
		return err; 
	
	}
	return -ENODEV;
}

int rt9396_ldo_enable(struct device *dev, unsigned num, unsigned enable)
{
	struct i2c_client *client;
	struct rt9396_driver_data *drvdata;
	int err = 0;

	if(rt9396_ref == NULL)
		return -ENODEV;
	
	
	drvdata = rt9396_ref;
	client = rt9396_ref->client;
	
	dprintk("ldo_no[%d], on/off[%d]\n",num, enable);
	if (num > 0 && num <= RT9396_LDO_NUM) {
		if(client) {
			if (enable) {
				if (drvdata->ldo_ref[num-1]++ == 0) {	
					printk("ref count = 0, call rt9396_set_ldos\n");
					err = rt9396_set_ldos(client, num, enable);
				}
			}
			else {
				if (--drvdata->ldo_ref[num-1] == 0) {
					printk("ref count = 0, call rt9396_set_ldos\n");
					err = rt9396_set_ldos(client, num, enable);
				}
			}
			return err;
		}
	}
	return -ENODEV;
}
EXPORT_SYMBOL(rt9396_ldo_enable);

static int rt9396_set_table(struct rt9396_driver_data *drvdata, struct rt9396_ctrl_tbl *ptbl)
{
	unsigned int i = 0;
	unsigned long delay = 0;

	if (ptbl == NULL) {
		eprintk("input ptr is null\n");
		return -EIO;
	}

	for( ;;) 
	{
		if (ptbl->reg == 0xFF) {
			if (ptbl->val != 0xFE) {
				delay = (unsigned long)ptbl->val;
				udelay(delay);
			}
			else
			break;
		}
		else {
		   if (rt9396_write(drvdata->client, ptbl->reg, ptbl->val) != 0)
			 dprintk("i2c failed addr:%d, value:%d\n", ptbl->reg, ptbl->val);
			}
		ptbl++;
		i++;
	}
	return 0;
}

static void rt9396_go_opmode(struct rt9396_driver_data *drvdata)
{	
	switch (drvdata->mode) {
		case NORMAL_MODE:
			rt9396_set_table(drvdata, drvdata->cmds.normal);
			drvdata->state = NORMAL_STATE;
			break;
		case ALC_MODE:	//not use
			break;
		default:
			eprintk("Invalid Mode\n");
			break;
	}
}

static void rt9396_device_init(struct rt9396_driver_data *drvdata)
{
	rt9396_go_opmode(drvdata);
}

/* This function provide sleep enter routine for power management. */
#ifdef CONFIG_PM
/*                                      
                                    
                                                                                                 
 */
typedef unsigned short word;
typedef unsigned char boolean;

extern void LGF_SendKey(word keycode);
extern boolean LGF_TestMode_Is_SleepMode(void);
//                                                
// minimum current at SMT
extern void set_operation_mode(bool info);
//                                       
/*                                       */

static void rt9396_sleep(struct rt9396_driver_data *drvdata)
{
	if (!drvdata || drvdata->state == SLEEP_STATE)
		return;	

	switch (drvdata->mode) {
		case NORMAL_MODE:
			drvdata->state = SLEEP_STATE;
			rt9396_set_table(drvdata, drvdata->cmds.sleep);			
			break;

		case ALC_MODE: //not use
			break;
		default:
			eprintk("Invalid Mode\n");
			break;
	}
	
	//                                                
	// minimum current at SMT
	if(LGF_TestMode_Is_SleepMode()) {
		set_operation_mode(false);
	} else {
		if(rt9396_powerstate == NORMAL_STATE){
			rt9396_powerstate = SLEEP_STATE;
		}
	}
	//                                       
}

static void rt9396_wakeup(struct rt9396_driver_data *drvdata)
{
	if (!drvdata || drvdata->state == NORMAL_STATE)
		return;
		
	if(rt9396_powerstate == SLEEP_STATE){
			rt9396_powerstate = NORMAL_STATE;
	}
//                                               
	//For backlight timing
	if(bl_chargerlogo == 1)
		msleep(50);
	else
		msleep(100);
//                                                
	if (drvdata->state == POWEROFF_STATE) {
		//rt9396_poweron(drvdata);
	} else if (drvdata->state == SLEEP_STATE) {
		if (drvdata->mode == NORMAL_MODE) 
		{
			rt9396_set_table(drvdata, drvdata->cmds.normal);			
			drvdata->state = NORMAL_STATE;
		} else if (drvdata->mode == ALC_MODE) {
			//nothing
		}
	}

	/*                                      
                                     
                                                                                                  
  */
	//                                                
	// minimum current at SMT
	if(LGF_TestMode_Is_SleepMode())
	{
		if(rt9396_powerstate == NORMAL_STATE )
		{
				LGF_SendKey(116);	// power key
				set_operation_mode(true);
		}
	}
	//                                              
	/*                                       */

}
#endif /* CONFIG_PM */

//                                                       
/*                                      
                                    
                                                                                                 
 */
int rt9396_get_state(void)
{
    return rt9396_powerstate;
}
/*                                       */

static int rt9396_send_intensity(struct rt9396_driver_data *drvdata, int next)
{
	if (drvdata->mode == NORMAL_MODE) {
		if (next > drvdata->max_intensity)
			next = drvdata->max_intensity;
		if (next < LCD_LED_MIN)
			next = LCD_LED_MIN;		

		if (drvdata->state == NORMAL_STATE && drvdata->intensity != next){			
			rt9396_write(drvdata->client, drvdata->reg_addrs.led_set_on, next);
		}
		drvdata->intensity = next;
	}
	else {
		dprintk("A manual setting for intensity is only permitted in normal mode\n");
	}
	return 0;

}


static int rt9396_get_intensity(struct rt9396_driver_data *drvdata)
{
	return drvdata->intensity;
}

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
static void rt9396_early_suspend(struct early_suspend * h)
{
	struct rt9396_driver_data *drvdata = container_of(h, struct rt9396_driver_data,
						    early_suspend);
	
	rt9396_sleep(drvdata);
	return;
}

static void rt9396_late_resume(struct early_suspend * h)
{
	struct rt9396_driver_data *drvdata = container_of(h, struct rt9396_driver_data,
						    early_suspend);
	
	rt9396_wakeup(drvdata);
	return;
}
#else
static int rt9396_suspend(struct i2c_client *i2c_dev, pm_message_t state)
{
	struct rt9396_driver_data *drvdata = i2c_get_clientdata(i2c_dev);
	rt9396_sleep(drvdata);
	return 0;
}

static int rt9396_resume(struct i2c_client *i2c_dev)
{
	struct rt9396_driver_data *drvdata = i2c_get_clientdata(i2c_dev);
	rt9396_wakeup(drvdata);
	return 0;
}
#endif	/* CONFIG_HAS_EARLYSUSPEND */
#else
#define rt9396_suspend NULL
#define rt9396_resume	NULL
#endif	/* CONFIG_PM */

void rt9396_switch_mode(struct device *dev, int next_mode)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(dev);

	if (!drvdata || drvdata->mode == next_mode)
		return;

	if (next_mode == ALC_MODE) {
      //not use
	}
	else if (next_mode == NORMAL_MODE) {		
	} else {
		printk(KERN_ERR "%s: invalid mode(%d)!!!\n", __func__, next_mode);
		return;
	}

	drvdata->mode = next_mode;
	return;
}

ssize_t rt9396_show_alc(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(dev->parent);
	int r;

	if (!drvdata) return 0;

	r = snprintf(buf, PAGE_SIZE, "%s\n", (drvdata->mode == ALC_MODE) ? "1":"0");

	return r;
}

ssize_t rt9396_store_alc(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int alc;
	int next_mode;

	if (!count)
		return -EINVAL;

	sscanf(buf, "%d", &alc);

	if (alc)
		next_mode = ALC_MODE;
	else
		next_mode = NORMAL_MODE;

	rt9396_switch_mode(dev->parent, next_mode);

	return count;
}

ssize_t rt9396_show_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(dev);
	int len = 0;
	unsigned char val;

	len += snprintf(buf, PAGE_SIZE, "\nrt9396 Registers is following..\n");
	rt9396_read(drvdata->client, 0x00, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[CH_EN(0x00)] = 0x%x\n", val);
	rt9396_read(drvdata->client, 0x01, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[BLM(0x01)] = 0x%x\n", val);
	rt9396_read(drvdata->client, 0x0E, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[ALS(0x0E)] = 0x%x\n", val);
	rt9396_read(drvdata->client, 0x0F, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[SBIAS(0x0F)] = 0x%x\n", val);
	rt9396_read(drvdata->client, 0x10, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[ALS_GAIN(0x10)] = 0x%x\n", val);
	rt9396_read(drvdata->client, 0x11, &val);
	len += snprintf(buf + len, PAGE_SIZE - len, "[AMBIENT_LEVEL(0x11)] = 0x%x\n", val);

	return len;
}

ssize_t rt9396_show_drvstat(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(dev->parent);
	int len = 0;

	len += snprintf(buf,  PAGE_SIZE,     "\nrt9396 Backlight Driver Status is following..\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "mode                   = %3d\n", drvdata->mode);
	len += snprintf(buf + len, PAGE_SIZE - len, "state                  = %3d\n", drvdata->state);
	len += snprintf(buf + len, PAGE_SIZE - len, "current intensity      = %3d\n", drvdata->intensity);

	return len;
}
ssize_t rt9396_store_chargerlogo(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int chargerlogo;	

	if (!count)
		return -EINVAL;

	sscanf(buf, "%d", &chargerlogo);	
	bl_chargerlogo = chargerlogo;	

	return count;
}


DEVICE_ATTR(alc, 0664, rt9396_show_alc, rt9396_store_alc);
DEVICE_ATTR(reg, 0444, rt9396_show_reg, NULL);
DEVICE_ATTR(drvstat, 0444, rt9396_show_drvstat, NULL);
DEVICE_ATTR(chargerlogo, 0664, NULL, rt9396_store_chargerlogo);

static int rt9396_set_brightness(struct backlight_device *bd)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(bd->dev.parent);
	return rt9396_send_intensity(drvdata, bd->props.brightness);
}

static int rt9396_get_brightness(struct backlight_device *bd)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(bd->dev.parent);
	return rt9396_get_intensity(drvdata);
}

static struct backlight_ops rt9396_ops = {
	.get_brightness = rt9396_get_brightness,
	.update_status  = rt9396_set_brightness,
};


#ifdef CONFIG_BACKLIGHT_LEDS_CLASS
static void leds_brightness_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct rt9396_driver_data *drvdata = dev_get_drvdata(led_cdev->dev->parent);
	int brightness;
	int next;	

	if (!drvdata) {
		eprintk("Error getting drvier data\n");
		return;
	}

	brightness = rt9396_get_intensity(drvdata);	
	if(value < MIN_VALUE)
	{
		if(value<0)
		{
			printk("%s, old value: %d\n", __func__,value );
			value=0;
		}			
		next = value*RT9396BL_MIN_BRIGHTNESS/MIN_VALUE;
	}
	else if(value >= MIN_VALUE && value <= DEFAULT_VALUE)

	{
		next = RT9396BL_MIN_BRIGHTNESS + (RT9396BL_DEFAULT_BRIGHTNESS - RT9396BL_MIN_BRIGHTNESS)
			*(value-MIN_VALUE)/( DEFAULT_VALUE - MIN_VALUE);
	}
	else if(value >DEFAULT_VALUE)
	{
		if(value>MAX_VALUE)
		{
			printk("%s, old value: %d\n", __func__,value );
			value=MAX_VALUE;
		}
		next = RT9396BL_DEFAULT_BRIGHTNESS + (RT9396BL_MAX_BRIGHTNESS  - RT9396BL_DEFAULT_BRIGHTNESS)
			*(value-DEFAULT_VALUE)/(MAX_VALUE - DEFAULT_VALUE);
	}
	
	if (brightness != next) {		
		printk("%s, value: %d,after tuning next: %d \n", __func__, value, next );
		rt9396_send_intensity(drvdata, next);
	}
}

static struct led_classdev rt9396_led_dev = {
	.name = LEDS_BACKLIGHT_NAME,
	.brightness_set = leds_brightness_set,
};
#endif

static int rt9396_probe(struct i2c_client *i2c_dev, const struct i2c_device_id *i2c_dev_id)
{
	struct lge_backlight_platform_data *pdata;
	struct rt9396_driver_data *drvdata;
	struct backlight_device *bd;
	int err;

	rt9396_powerstate = NORMAL_STATE;
	dprintk("start, client addr=0x%x\n", i2c_dev->addr);

	pdata = i2c_dev->dev.platform_data;
	if(!pdata)
		return -EINVAL;

	drvdata = kzalloc(sizeof(struct rt9396_driver_data), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&i2c_dev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	if (pdata && pdata->platform_init)
		pdata->platform_init();

	drvdata->client = i2c_dev;
	drvdata->gpio = pdata->gpio;
	drvdata->max_intensity = LCD_LED_MAX;
	if (pdata->max_current > 0)
		drvdata->max_intensity = pdata->max_current;
	drvdata->intensity = LCD_LED_MIN;
	drvdata->mode = NORMAL_MODE;
	drvdata->state = UNINIT_STATE;
	drvdata->version = pdata->version;

	if(rt9396_setup_version(drvdata) != 0) {
		eprintk("Error while requesting gpio %d\n", drvdata->gpio);
		kfree(drvdata);
		return -ENODEV;
	}

#if 0 //not use enable pin in rt9396	
	if (drvdata->gpio && gpio_request(drvdata->gpio, "rt9396_en") != 0) {
		eprintk("Error while requesting gpio %d\n", drvdata->gpio);
		kfree(drvdata);
		return -ENODEV;
	}
#endif

	bd = backlight_device_register("rt9396-bl", &i2c_dev->dev, NULL, &rt9396_ops, NULL);
	if (bd == NULL) {
		eprintk("entering rt9396 probe function error \n");
		//if (gpio_is_valid(drvdata->gpio))
		//	gpio_free(drvdata->gpio);
		kfree(drvdata);
		return -1;
	}
	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = drvdata->intensity;
	bd->props.max_brightness = drvdata->max_intensity;
	drvdata->bd = bd;

#ifdef CONFIG_BACKLIGHT_LEDS_CLASS
	if (led_classdev_register(&i2c_dev->dev, &rt9396_led_dev) == 0) {
		eprintk("Registering led class dev successfully.\n");
		drvdata->led = &rt9396_led_dev;
		err = device_create_file(drvdata->led->dev, &dev_attr_alc);
		err = device_create_file(drvdata->led->dev, &dev_attr_reg);
		err = device_create_file(drvdata->led->dev, &dev_attr_drvstat);
		err = device_create_file(drvdata->led->dev, &dev_attr_chargerlogo);
	}
#endif

	i2c_set_clientdata(i2c_dev, drvdata);
	i2c_set_adapdata(i2c_dev->adapter, i2c_dev);

	rt9396_device_init(drvdata);
	rt9396_send_intensity(drvdata, RT9396BL_DEFAULT_BRIGHTNESS);

#ifdef CONFIG_HAS_EARLYSUSPEND
	drvdata->early_suspend.suspend = rt9396_early_suspend;
	drvdata->early_suspend.resume = rt9396_late_resume;
	drvdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 40;
	register_early_suspend(&drvdata->early_suspend);
#endif

	rt9396_ref = drvdata;

	eprintk("done\n");
	return 0;
}

static int __devexit rt9396_remove(struct i2c_client *i2c_dev)
{
	struct rt9396_driver_data *drvdata = i2c_get_clientdata(i2c_dev);

	rt9396_send_intensity(drvdata, 0);

	backlight_device_unregister(drvdata->bd);
	led_classdev_unregister(drvdata->led);
	i2c_set_clientdata(i2c_dev, NULL);
	//if (gpio_is_valid(drvdata->gpio))
	//	gpio_free(drvdata->gpio);
	kfree(drvdata);

	return 0;
}

static struct i2c_device_id rt9396_idtable[] = {
	{ MODULE_NAME, 0 },
};

MODULE_DEVICE_TABLE(i2c, rt9396_idtable);

static struct i2c_driver rt9396_driver = {
	.probe 		= rt9396_probe,
	.remove 	= rt9396_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend 	= rt9396_suspend,
	.resume 	= rt9396_resume,
#endif
	.id_table 	= rt9396_idtable,
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
	},
};


/*                                                                              */
static int rt9396_send_off(struct notifier_block *this,
				unsigned long event, void *cmd)
{
	
	if ((event == SYS_RESTART) || (event == SYS_POWER_OFF))
	    	rt9396_send_intensity(rt9396_ref, 0);
	
	return NOTIFY_DONE;
}

struct notifier_block lge_chg_reboot_nb = {
	.notifier_call = rt9396_send_off, 
};

extern int register_reboot_notifier(struct notifier_block *nb);
/*                                                                              */



static int __init rt9396_init(void)
{
	printk("rt9396 init start\n");

 /*                                                                              */
       register_reboot_notifier(&lge_chg_reboot_nb);
 /*                                                                              */
 
	return i2c_add_driver(&rt9396_driver);
}

static void __exit rt9396_exit(void)
{
	i2c_del_driver(&rt9396_driver);
}

module_init(rt9396_init);
module_exit(rt9396_exit);

MODULE_DESCRIPTION("Backlight driver for ROHM rt9396");
MODULE_AUTHOR("Sohyun Nam <sohyun.nam@lge.com>");
MODULE_LICENSE("GPL");
