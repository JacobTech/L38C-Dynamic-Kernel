/*
 *  drivers/input/sensor/k3dh.c
 *
 *  Copyright (c) 2010 LGE.
 *
 *  All source code in this file is licensed under the following license
 *  except where indicated.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, you can find it at http://www.fsf.org
 */
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <asm/gpio.h>
#include <linux/input-polldev.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include <mach/board_lge.h> // platform data
//#include <linux/akm8973.h>	// akm daemon ioctl set

#undef CONFIG_HAS_EARLYSUSPEND
#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>

struct early_suspend k3dh_sensor_early_suspend;

static void k3dh_early_suspend(struct early_suspend *h);
static void k3dh_late_resume(struct early_suspend *h);
#endif
#define USE_WORK_QUEUE        0

/** Maximum polled-device-reported g value */
#define G_MAX			16000 // 8000

#define SHIFT_ADJ_2G		4
#define SHIFT_ADJ_4G		3
#define SHIFT_ADJ_8G		2

#define AXISDATA_REG		0x28

#define CTRL_REG1		0x20	/* ctrl 1: pm2 pm1 pm0 dr1 dr0 zenable yenable zenable */
#define CTRL_REG2		0x21	/* filter setting */
#define CTRL_REG3		0x22	/* interrupt control reg */
#define CTRL_REG4		0x23
#define CTRL_REG5		0x23	/* scale selection */

#define PM_OFF          	0x00
#define PM_NORMAL       	0x20
#define ENABLE_ALL_AXES 	0x07

#define ODRHALF         	0x40	/* 0.5Hz output data rate */
#define ODR1            	0x60	/* 1Hz output data rate */
#define ODR2            	0x80	/* 2Hz output data rate */
#define ODR5            	0xA0	/* 5Hz output data rate */
#define ODR10           	0xC0	/* 10Hz output data rate */
#define ODR50           	0x00	/* 50Hz output data rate */
#define ODR100          	0x08	/* 100Hz output data rate */
#define ODR400          	0x10	/* 400Hz output data rate */
#define ODR1000         	0x18	/* 1000Hz output data rate */

#define FUZZ			32
#define FLAT			32
#define I2C_RETRY_DELAY		5
#define I2C_RETRIES		5
#define AUTO_INCREMENT		0x80

/** The following define the IOCTL command values via the ioctl macros */
#define K3DH_IOCTL_BASE 'a'
#define K3DH_IOCTL_SET_DELAY		_IOW(K3DH_IOCTL_BASE, 0, int)
#define K3DH_IOCTL_GET_DELAY		_IOR(K3DH_IOCTL_BASE, 1, int)
#define K3DH_IOCTL_SET_ENABLE		_IOW(K3DH_IOCTL_BASE, 2, int)
#define K3DH_IOCTL_GET_ENABLE		_IOR(K3DH_IOCTL_BASE, 3, int)
#define K3DH_IOCTL_SET_G_RANGE		_IOW(K3DH_IOCTL_BASE, 4, int)
#define K3DH_IOCTL_READ_ACCEL_XYZ	_IOW(K3DH_IOCTL_BASE, 8, int)

#define OUT_X          0x29
#define OUT_Y          0x2B
#define OUT_Z          0x2D
#define K3DH_G_2G 0x00
#define K3DH_G_4G 0x10
#define K3DH_G_8G 0x30

#define WHO_AM_I		0x0F

#define K3DH_DEVICE_ID	0x33

#define K3DH_OPEN_ENABLE

struct {
	unsigned int cutoff;
	unsigned int mask;
} odr_table[] = {
	{ 3,	PM_NORMAL | ODR1000}, 
	{ 10,	PM_NORMAL | ODR400}, 
	{ 20,	PM_NORMAL | ODR100}, 
	{ 100,	PM_NORMAL | ODR50}, 
	{ 200,	ODR1000	| ODR10}, 
	{ 500,	ODR1000 | ODR5}, 
	{ 1000,	ODR1000 | ODR2}, 
	{ 2000,	ODR1000 | ODR1}, 
	{ 0,	ODR1000 | ODRHALF},
};

struct k3dh_data {
	struct i2c_client *client;
	struct k3dh_platform_data *pdata;

	struct mutex lock;

	struct delayed_work input_work;
	struct input_dev *input_dev;

	int hw_initialized;
	atomic_t enabled;
	int on_before_suspend;

	u8 shift_adj;
	u8 resume_state[5];
};

//                                                             
static unsigned char k3dh_xyz[3] = {0,};

/*
 * Because misc devices can not carry a pointer from driver register to
 * open, we keep this global.  This limits the driver to a single instance.
 */
struct k3dh_data *k3dh_misc_data;

static int k3dh_i2c_read(struct k3dh_data *kr, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = kr->client->addr,
			.flags = kr->client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = kr->client->addr,
			.flags = (kr->client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};

	do {
		err = i2c_transfer(kr->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&kr->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int k3dh_i2c_write(struct k3dh_data *kr, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = kr->client->addr,
		 .flags = kr->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(kr->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&kr->client->dev, "write transfer error\n");
		err = -EIO;
	} 
	else {
		err = 0;
	}

	return err;
}

static int k3dh_hw_init(struct k3dh_data *kr)
{
	int err = -1;
	u8 buf[6];

	buf[0] = (AUTO_INCREMENT | CTRL_REG1);
	buf[1] = kr->resume_state[0];
	buf[2] = kr->resume_state[1];
	buf[3] = kr->resume_state[2];
	buf[4] = kr->resume_state[3];
	buf[5] = kr->resume_state[4];
	err = k3dh_i2c_write(kr, buf, 5);
	if (err < 0)
		return err;

	kr->hw_initialized = 1;

	return 0;
}

static void k3dh_device_power_off(struct k3dh_data *kr)
{
	int err;
	u8 buf[2] = {CTRL_REG1, PM_OFF};

	err = k3dh_i2c_write(kr, buf, 1);
	if (err < 0)
		dev_err(&kr->client->dev, "soft power off failed\n");

	if (kr->pdata->power_off) {
		kr->pdata->power_off();
		kr->hw_initialized = 0;
	}
}

static int k3dh_device_power_on(struct k3dh_data *kr)
{

	int err;

	if (kr->pdata->power_on) {
		err = kr->pdata->power_on();
		if (err < 0)
			return err;

		mdelay(100);	// K3DH boot up time
	}

	if (!kr->hw_initialized) {
		err = k3dh_hw_init(kr);
		if (err < 0) {
			k3dh_device_power_off(kr);
			return err;
		}
	}

	return 0;
}

int k3dh_update_g_range(struct k3dh_data *kr, u8 new_g_range)
{
	int err;
	u8 shift;
	u8 buf[2];

	switch (new_g_range) {
		case K3DH_G_2G:
			shift = SHIFT_ADJ_2G;
			break;
		case K3DH_G_4G:
			shift = SHIFT_ADJ_4G;
			break;
		case K3DH_G_8G:
			shift = SHIFT_ADJ_8G;
			break;
		default:
			return -EINVAL;
	}

	if (atomic_read(&kr->enabled)) {
		/* Set configuration register 4, which contains g range setting
		 *  NOTE: this is a straight overwrite because this driver does
		 *  not use any of the other configuration bits in this
		 *  register.  Should this become untrue, we will have to read
		 *  out the value and only change the relevant bits --XX----
		 *  (marked by X) */
		buf[0] = CTRL_REG4;
		buf[1] = new_g_range;
		err = k3dh_i2c_write(kr, buf, 1);
		if (err < 0)
			return err;
	}

	kr->resume_state[3] = new_g_range;
	kr->shift_adj = shift;

	return 0;
}

int k3dh_update_odr(struct k3dh_data *kr, int poll_interval)
{
	int err = -1;
	int i;
	u8 config[2];

	/* Convert the poll interval into an output data rate configuration
	 *  that is as low as possible.  The ordering of these checks must be
	 *  maintained due to the cascading cut off values - poll intervals are
	 *  checked from shortest to longest.  At each check, if the next lower
	 *  ODR cannot support the current poll interval, we stop searching */
	for (i = 0; i < ARRAY_SIZE(odr_table); i++) {
		config[1] = odr_table[i].mask;
		if (poll_interval < odr_table[i].cutoff)
			break;
	}

	config[1] |= ENABLE_ALL_AXES;

	/* If device is currently enabled, we need to write new
	 *  configuration out to it */
	if (atomic_read(&kr->enabled)) {
		config[0] = CTRL_REG1;
		err = k3dh_i2c_write(kr, config, 1);
		if (err < 0)
			return err;
	}

	kr->resume_state[0] = config[1];

	return 0;
}

static int k3dh_get_acceleration_data(struct k3dh_data *kr, int *xyz)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[6];
	/* x,y,z hardware data */
	int hw_d[3] = { 0 };

	acc_data[0] = (AUTO_INCREMENT | AXISDATA_REG);

	
	err = k3dh_i2c_read(kr, acc_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = (int) (((acc_data[1]) << 8) | acc_data[0]);
	hw_d[1] = (int) (((acc_data[3]) << 8) | acc_data[2]);
	hw_d[2] = (int) (((acc_data[5]) << 8) | acc_data[4]);

	//                                                             
	k3dh_xyz[0] = (unsigned char)(hw_d[0] >> 4);
	k3dh_xyz[1] = (unsigned char)(hw_d[1] >> 4);
	k3dh_xyz[2] = (unsigned char)(hw_d[2] >> 4);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= kr->shift_adj;
	hw_d[1] >>= kr->shift_adj;
	hw_d[2] >>= kr->shift_adj;

	xyz[0] = ((kr->pdata->negate_x) ? (-hw_d[kr->pdata->axis_map_x])
		  : (hw_d[kr->pdata->axis_map_x]));
	xyz[1] = ((kr->pdata->negate_y) ? (-hw_d[kr->pdata->axis_map_y])
		  : (hw_d[kr->pdata->axis_map_y]));
	xyz[2] = ((kr->pdata->negate_z) ? (-hw_d[kr->pdata->axis_map_z])
		  : (hw_d[kr->pdata->axis_map_z]));

	//dev_info(&kr->client->dev, "(x, y ,z) = (%d, %d, %d)\n", xyz[0],xyz[1], xyz[2]);
	
	return err;
}

#if USE_WORK_QUEUE
static void k3dh_report_values(struct k3dh_data *kr, int *xyz)
{
	input_report_abs(kr->input_dev, ABS_X, xyz[0]);
	input_report_abs(kr->input_dev, ABS_Y, xyz[1]);
	input_report_abs(kr->input_dev, ABS_Z, xyz[2]);
	input_sync(kr->input_dev);

	//dev_info(&kr->client->dev, "(x, y ,z) = (%d, %d, %d)\n", xyz[0],xyz[1], xyz[2]);
}
#endif

static int k3dh_enable(struct k3dh_data *kr)
{
	int err;

	if (!atomic_cmpxchg(&kr->enabled, 0, 1)) {

		err = k3dh_device_power_on(kr);
		if (err < 0) {
			atomic_set(&kr->enabled, 0);
			return err;
		}
#if USE_WORK_QUEUE
		schedule_delayed_work(&kr->input_work,
			      msecs_to_jiffies(kr->pdata->poll_interval));
#endif

	}

	return 0;
}

static int k3dh_disable(struct k3dh_data *kr)
{
	if (atomic_cmpxchg(&kr->enabled, 1, 0)) {
#if USE_WORK_QUEUE
		cancel_delayed_work_sync(&kr->input_work);
#endif
		k3dh_device_power_off(kr);
	}

	return 0;
}

static int k3dh_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = k3dh_misc_data;

	return 0;
}

static long k3dh_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int buf[3];
	int err;
	int interval;
	struct k3dh_data *kr = file->private_data;

	switch (cmd) {
	case K3DH_IOCTL_GET_DELAY:
		interval = kr->pdata->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;

	case K3DH_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 200)
			return -EINVAL;

		kr->pdata->poll_interval =
		    max(interval, kr->pdata->min_interval);
		err = k3dh_update_odr(kr, kr->pdata->poll_interval);
		/* TODO: if update fails poll is still set */
		if (err < 0)
			return err;

		break;

	case K3DH_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;

		if (interval)
			k3dh_enable(kr);
		else
			k3dh_disable(kr);

		break;

	case K3DH_IOCTL_GET_ENABLE:
		interval = atomic_read(&kr->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;

		break;

	case K3DH_IOCTL_SET_G_RANGE:
		if (copy_from_user(&buf, argp, 1))
			return -EFAULT;
		err = k3dh_update_g_range(kr, arg);
		if (err < 0)
			return err;

		break;

	case K3DH_IOCTL_READ_ACCEL_XYZ:
		err=k3dh_get_acceleration_data(kr, buf);
		if (err < 0)
				return err;

		if (copy_to_user(argp, buf, sizeof(int)*3))
			return -EINVAL;

		return err;

		break;
#if 0
	case AKMD2_TO_ACCEL_IOCTL_READ_XYZ:
		err=k3dh_get_acceleration_data(kr, buf);
		if (err < 0)
				return err;

		if (copy_to_user(argp, buf, sizeof(int)*3))
			return -EINVAL;

		return err;

		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static struct file_operations k3dh_misc_fops = {
	.owner = THIS_MODULE,
	.open = k3dh_misc_open,
	.unlocked_ioctl = k3dh_misc_ioctl,
};

static struct miscdevice k3dh_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "K3DH",
	.fops = &k3dh_misc_fops,
};

#if USE_WORK_QUEUE
static void k3dh_input_work_func(struct work_struct *work)
{
	struct k3dh_data *kr = container_of((struct delayed_work *)work,
						  struct k3dh_data,
						  input_work);
	int xyz[3] = { 0 };
	int err;

	mutex_lock(&kr->lock);
	err = k3dh_get_acceleration_data(kr, xyz);
	if (err < 0)
		dev_err(&kr->client->dev, "get_acceleration_data failed\n");
	else
		k3dh_report_values(kr, xyz);

	schedule_delayed_work(&kr->input_work,
			      msecs_to_jiffies(kr->pdata->poll_interval));
	mutex_unlock(&kr->lock);
}
#endif // USE_WORK_QUEUE

#ifdef K3DH_OPEN_ENABLE
int k3dh_input_open(struct input_dev *input)
{
	struct k3dh_data *kr = input_get_drvdata(input);
	return k3dh_enable(kr);
}

void k3dh_input_close(struct input_dev *dev)
{
	struct k3dh_data *kr = input_get_drvdata(dev);
	
	k3dh_disable(kr);
}
#endif

static int k3dh_validate_pdata(struct k3dh_data *kr)
{
	kr->pdata->poll_interval = max(kr->pdata->poll_interval,
					kr->pdata->min_interval);

	if (kr->pdata->axis_map_x > 2 ||
	    kr->pdata->axis_map_y > 2 || kr->pdata->axis_map_z > 2) {
		dev_err(&kr->client->dev,
			"invalid axis_map value x:%u y:%u z%u\n",
			kr->pdata->axis_map_x, kr->pdata->axis_map_y,
			kr->pdata->axis_map_z);
		return -EINVAL;
	}

	/* Only allow 0 and 1 for negation boolean flag */
	if (kr->pdata->negate_x > 1 || kr->pdata->negate_y > 1 ||
	    kr->pdata->negate_z > 1) {
		dev_err(&kr->client->dev,
			"invalid negate value x:%u y:%u z:%u\n",
			kr->pdata->negate_x, kr->pdata->negate_y,
			kr->pdata->negate_z);
		return -EINVAL;
	}

	/* Enforce minimum polling interval */
	if (kr->pdata->poll_interval < kr->pdata->min_interval) {
		dev_err(&kr->client->dev, "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}

static int k3dh_input_init(struct k3dh_data *kr)
{
	int err;

#if USE_WORK_QUEUE
	INIT_DELAYED_WORK(&kr->input_work, k3dh_input_work_func);

#endif
	kr->input_dev = input_allocate_device();
	if (!kr->input_dev) {
		err = -ENOMEM;
		dev_err(&kr->client->dev, "input device allocate failed\n");
		goto err0;
	}

#ifdef K3DH_OPEN_ENABLE
	kr->input_dev->open = k3dh_input_open;
	kr->input_dev->close = k3dh_input_close;
#endif

	input_set_drvdata(kr->input_dev, kr);

	set_bit(EV_ABS, kr->input_dev->evbit);

	input_set_abs_params(kr->input_dev, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(kr->input_dev, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(kr->input_dev, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);

	kr->input_dev->name = "accelerometer";

	err = input_register_device(kr->input_dev);
	if (err) {
		dev_err(&kr->client->dev,
			"unable to register input polled device %s\n",
			kr->input_dev->name);
		goto err1;
	}


	return 0;

err1:
	input_free_device(kr->input_dev);
err0:
	return err;
}

static void k3dh_input_cleanup(struct k3dh_data *kr)
{
	input_unregister_device(kr->input_dev);
	input_free_device(kr->input_dev);
}

//                                                   
static ssize_t k3dh_x_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	
	return sprintf(buf, "%d\n", k3dh_xyz[0]);
}

static ssize_t k3dh_y_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	
	return sprintf(buf, "%d\n", k3dh_xyz[1]);
}

static ssize_t k3dh_z_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	
	return sprintf(buf, "%d\n", k3dh_xyz[2]);
}

static DEVICE_ATTR(x, S_IRUGO, k3dh_x_show, NULL);
static DEVICE_ATTR(y, S_IRUGO, k3dh_y_show, NULL);
static DEVICE_ATTR(z, S_IRUGO, k3dh_z_show, NULL);

static struct attribute *dev_attrs[] = {
	&dev_attr_x.attr,
	&dev_attr_y.attr,
	&dev_attr_z.attr,
	NULL
};

static struct attribute_group dev_attr_grp = {
	.attrs = dev_attrs,
};
//                                                   

static int k3dh_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct k3dh_data *kr;
	int err = -1;
	u8 id_check = WHO_AM_I;
	int sysfsret;

	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto err0;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto err0;
	}

	kr = kzalloc(sizeof(struct k3dh_data), GFP_KERNEL);
	if (kr == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		err = -ENOMEM;
		printk("k3dh_data kmalloc Error\n");
		goto err0;
	}
	
	mutex_init(&kr->lock);
	mutex_lock(&kr->lock);
	kr->client = client;

	kr->pdata = kmalloc(sizeof(struct k3dh_platform_data), GFP_KERNEL);
	if (kr->pdata == NULL){
		printk("k3dh_platform_data kmalloc Error\n");
		goto err1;
	}

	memcpy(kr->pdata, client->dev.platform_data, sizeof(*kr->pdata));

	err = k3dh_validate_pdata(kr);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data\n");
		goto err1_1;
	}

	i2c_set_clientdata(client, kr);

	if (kr->pdata->kr_init) {
		err = kr->pdata->kr_init();
		if (err < 0)
			goto err1_1;
	}

	memset(kr->resume_state, 0, ARRAY_SIZE(kr->resume_state));

	/* control register setting */
	kr->resume_state[0] = PM_NORMAL | ENABLE_ALL_AXES;
	kr->resume_state[1] = 0;
	kr->resume_state[2] = 0;
	kr->resume_state[3] = 0;
	kr->resume_state[4] = 0;

	err = k3dh_device_power_on(kr);
	if (err < 0){
		dev_err(&client->dev, "Power On Fail");
		goto err2;
	}

	err = k3dh_i2c_read(kr, &id_check, 1);
	if(id_check != K3DH_DEVICE_ID)
	{
		dev_err(&client->dev, "Device ID not matched\n");
		err = -ENODEV;
		goto err2;
	}

	atomic_set(&kr->enabled, 1);

	err = k3dh_update_g_range(kr, kr->pdata->g_range);
	if (err < 0) {
		dev_err(&client->dev, "update_g_range failed\n");
		goto err2;
	}

	err = k3dh_update_odr(kr, kr->pdata->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr failed\n");
		goto err2;
	}

	err = k3dh_input_init(kr);
	
	if (err < 0){
		printk("%s : K3DH Probe k3dh device init FAIL!\n",__func__);	
		goto err3;
	}

	k3dh_misc_data = kr;

	err = misc_register(&k3dh_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "krd_device register failed\n");
		goto err4;
	}

#if defined(CONFIG_HAS_EARLYSUSPEND)
	k3dh_sensor_early_suspend.suspend = k3dh_early_suspend;
	k3dh_sensor_early_suspend.resume = k3dh_late_resume;
	k3dh_sensor_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 45;
	register_early_suspend(&k3dh_sensor_early_suspend);
#endif

#if 0
	k3dh_device_power_off(kr);

	/* As default, do not report information */
	atomic_set(&kr->enabled, 0);
#endif

	mutex_unlock(&kr->lock);

	//                                                 
	sysfsret=sysfs_create_group(&client->dev.kobj, &dev_attr_grp);
	if(sysfsret)
	{
		printk("failed to create device file in sysfs\n");
		goto err4;
	}
	dev_info(&client->dev, "%s k3dh: Accelerometer chip found\n", client->name);
    printk(KERN_DEBUG "accel probe done\n");
	return 0;

err4:
	k3dh_input_cleanup(kr);
err3:
	k3dh_device_power_off(kr);
err2:
	if (kr->pdata->kr_exit)
		kr->pdata->kr_exit();
err1_1:
	mutex_unlock(&kr->lock);
	kfree(kr->pdata);
err1:
	kfree(kr);
err0:
	return err;
}

static int __devexit k3dh_remove(struct i2c_client *client)
{
	/* TODO: revisit ordering here once _probe order is finalized */
	struct k3dh_data *kr = i2c_get_clientdata(client);

	//                                                 
	sysfs_remove_group(&client->dev.kobj, &dev_attr_grp);

	misc_deregister(&k3dh_misc_device);
	k3dh_input_cleanup(kr);
	k3dh_device_power_off(kr);
	if (kr->pdata->kr_exit)
		kr->pdata->kr_exit();
	kfree(kr->pdata);
	kfree(kr);

#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&k3dh_sensor_early_suspend);
#endif

	return 0;
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
static void k3dh_early_suspend(struct early_suspend *h)
{
	k3dh_disable(k3dh_misc_data);
}

static void k3dh_late_resume(struct early_suspend *h)
{
	k3dh_enable(k3dh_misc_data);
}
#endif

#if defined(CONFIG_PM)
static int k3dh_resume(struct device *device)
{
	struct i2c_client *client = i2c_verify_client(device);
	struct k3dh_data *kr = i2c_get_clientdata(client);

	kr->pdata->gpio_config(1);
	return k3dh_enable(kr);
}

static int k3dh_suspend(struct device *device)
{
	struct i2c_client *client = i2c_verify_client(device);
	struct k3dh_data *kr = i2c_get_clientdata(client);

	kr->pdata->gpio_config(0);
	return k3dh_disable(kr);
}
#endif

static const struct i2c_device_id k3dh_id[] = {
	{"K3DH", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, k3dh_id);

#if defined(CONFIG_PM)
static struct dev_pm_ops k3dh_pm_ops = {
       .suspend = k3dh_suspend,
       .resume = k3dh_resume,
};
#endif

static struct i2c_driver k3dh_driver = {
	.driver = {
		   .name = "K3DH",

#if defined(CONFIG_PM)
		   .pm = &k3dh_pm_ops,
#endif
		   },
	.probe = k3dh_probe,
	.remove = __devexit_p(k3dh_remove),
	.id_table = k3dh_id,
};

static int __init k3dh_init(void)
{
	pr_info("K3DH accelerometer driver\n");

	return i2c_add_driver(&k3dh_driver);
}

static void __exit k3dh_exit(void)
{
	i2c_del_driver(&k3dh_driver);
	return;
}

module_init(k3dh_init);
module_exit(k3dh_exit);

MODULE_DESCRIPTION("k3dh accelerometer driver");
MODULE_AUTHOR("STMicroelectronics");
