
/* Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
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
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/leds-pmic8058.h>
#include <linux/pwm.h>
#include <linux/pmic8058-pwm.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <mach/pmic.h>
#include <mach/camera.h>
#include <mach/gpio.h>

struct timer_list timer_flash;

enum msm_cam_flash_stat{
	MSM_CAM_FLASH_OFF,
	MSM_CAM_FLASH_ON,
};

//                                                      
#ifdef CONFIG_MSM_CAMERA_FLASH_LM2759
extern int lm2759_flash_set_led_state(int state);
#endif
//                            

#ifdef CONFIG_MSM_CAMERA_FLASH_LM3559
/*           
                                 
                                  
 */
extern int lm3559_flash_set_led_state(int state);
#endif

#if defined CONFIG_MSM_CAMERA_FLASH_SC628A
static struct sc628a_work_t *sc628a_flash;
static struct i2c_client *sc628a_client;
static DECLARE_WAIT_QUEUE_HEAD(sc628a_wait_queue);

struct sc628a_work_t {
	struct work_struct work;
};

static const struct i2c_device_id sc628a_i2c_id[] = {
	{"sc628a", 0},
	{ }
};

static int32_t sc628a_i2c_txdata(unsigned short saddr,
		unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = saddr,
			.flags = 0,
			.len = length,
			.buf = txdata,
		},
	};
	if (i2c_transfer(sc628a_client->adapter, msg, 1) < 0) {
		pr_err("sc628a_i2c_txdata faild 0x%x\n", saddr);
		return -EIO;
	}

	return 0;
}

static int32_t sc628a_i2c_write_b_flash(uint8_t waddr, uint8_t bdata)
{
	int32_t rc = -EFAULT;
	unsigned char buf[2];

	memset(buf, 0, sizeof(buf));
	buf[0] = waddr;
	buf[1] = bdata;

	rc = sc628a_i2c_txdata(sc628a_client->addr, buf, 2);
	if (rc < 0) {
		pr_err("i2c_write_b failed, addr = 0x%x, val = 0x%x!\n",
				waddr, bdata);
	}
	return rc;
}

static int sc628a_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&sc628a_wait_queue);
	return 0;
}

static int sc628a_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int rc = 0;
	CDBG("sc628a_probe called!\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("i2c_check_functionality failed\n");
		goto probe_failure;
	}

	sc628a_flash = kzalloc(sizeof(struct sc628a_work_t), GFP_KERNEL);
	if (!sc628a_flash) {
		pr_err("kzalloc failed.\n");
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, sc628a_flash);
	sc628a_init_client(client);
	sc628a_client = client;

	msleep(50);

	CDBG("sc628a_probe successed! rc = %d\n", rc);
	return 0;

probe_failure:
	pr_err("sc628a_probe failed! rc = %d\n", rc);
	return rc;
}

static struct i2c_driver sc628a_i2c_driver = {
	.id_table = sc628a_i2c_id,
	.probe  = sc628a_i2c_probe,
	.remove = __exit_p(sc628a_i2c_remove),
	.driver = {
		.name = "sc628a",
	},
};
#endif

static int config_flash_gpio_table(enum msm_cam_flash_stat stat,
			struct msm_camera_sensor_strobe_flash_data *sfdata)
{
	int rc = 0, i = 0;
	int msm_cam_flash_gpio_tbl[][2] = {
		{sfdata->flash_trigger, 1},
		{sfdata->flash_charge, 1},
		{sfdata->flash_charge_done, 0}
	};

	if (stat == MSM_CAM_FLASH_ON) {
		for (i = 0; i < ARRAY_SIZE(msm_cam_flash_gpio_tbl); i++) {
			rc = gpio_request(msm_cam_flash_gpio_tbl[i][0],
							  "CAM_FLASH_GPIO");
			if (unlikely(rc < 0)) {
				pr_err("%s not able to get gpio\n", __func__);
				for (i--; i >= 0; i--)
					gpio_free(msm_cam_flash_gpio_tbl[i][0]);
				break;
			}
			if (msm_cam_flash_gpio_tbl[i][1])
				gpio_direction_output(
					msm_cam_flash_gpio_tbl[i][0], 0);
			else
				gpio_direction_input(
					msm_cam_flash_gpio_tbl[i][0]);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(msm_cam_flash_gpio_tbl); i++) {
			gpio_direction_input(msm_cam_flash_gpio_tbl[i][0]);
			gpio_free(msm_cam_flash_gpio_tbl[i][0]);
		}
	}
	return rc;
}
//                                               
#ifdef CONFIG_MSM_CAMERA_FLASH_LM2759
int msm_camera_flash_lm2759(unsigned led_state)
{
	int rc = 0;

	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
		rc = lm2759_flash_set_led_state(0);
		break;

	case MSM_CAMERA_LED_LOW:
		rc = lm2759_flash_set_led_state(1);
		break;

	case MSM_CAMERA_LED_HIGH:
		rc = lm2759_flash_set_led_state(2);
		break;
		/*
	case MSM_CAMERA_LED_AGC_STATE:
		rc = lm2759_flash_set_led_state(3);
		break;
	case MSM_CAMERA_LED_TORCH:
		rc = lm2759_flash_set_led_state(4);
		break;
		*/
	default:
		rc = -EFAULT;
		break;
	}

	CDBG("flash_set_led_state: return %d\n", rc);

	return rc;
}
#endif
//                            

#ifdef CONFIG_MSM_CAMERA_FLASH_LM3559
/*           
                                 
                                  
 */
int msm_camera_flash_lm3559(unsigned led_state)
{
	int rc = 0;

	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
	case MSM_CAMERA_LED_LOW:
	case MSM_CAMERA_LED_HIGH:
		rc = lm3559_flash_set_led_state(led_state);
		break;
	default:
		rc = -EFAULT;
		break;
	}

	CDBG("%s: led_state = %d, return %d\n", __func__, led_state, rc);
	return rc;
}
#endif

int msm_camera_flash_current_driver(
	struct msm_camera_sensor_flash_current_driver *current_driver,
	unsigned led_state)
{
	int rc = 0;
#if defined CONFIG_LEDS_PMIC8058
	int idx;
	const struct pmic8058_leds_platform_data *driver_channel =
		current_driver->driver_channel;
	int num_leds = driver_channel->num_leds;

	CDBG("%s: led_state = %d\n", __func__, led_state);

	/* Evenly distribute current across all channels */
	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
		for (idx = 0; idx < num_leds; ++idx) {
			rc = pm8058_set_led_current(
				driver_channel->leds[idx].id, 0);
			if (rc < 0)
				pr_err(
					"%s: FAIL name = %s, rc = %d\n",
					__func__,
					driver_channel->leds[idx].name,
					rc);
		}
		break;

	case MSM_CAMERA_LED_LOW:
		for (idx = 0; idx < num_leds; ++idx) {
			rc = pm8058_set_led_current(
				driver_channel->leds[idx].id,
				current_driver->low_current/num_leds);
			if (rc < 0)
				pr_err(
					"%s: FAIL name = %s, rc = %d\n",
					__func__,
					driver_channel->leds[idx].name,
					rc);
		}
		break;

	case MSM_CAMERA_LED_HIGH:
		for (idx = 0; idx < num_leds; ++idx) {
			rc = pm8058_set_led_current(
				driver_channel->leds[idx].id,
				current_driver->high_current/num_leds);
			if (rc < 0)
				pr_err(
					"%s: FAIL name = %s, rc = %d\n",
					__func__,
					driver_channel->leds[idx].name,
					rc);
		}
		break;

	default:
		rc = -EFAULT;
		break;
	}
	CDBG("msm_camera_flash_led_pmic8058: return %d\n", rc);
#endif /* CONFIG_LEDS_PMIC8058 */
#if defined CONFIG_MSM_CAMERA_FLASH_SC628A
	if (!sc628a_client) {
		rc = i2c_add_driver(&sc628a_i2c_driver);
		if (rc < 0 || sc628a_client == NULL) {
			rc = -ENOTSUPP;
			pr_err("I2C add driver failed");
			return rc;
		}
		rc = gpio_request(current_driver->led1, "sc628a");
		if (!rc) {
			gpio_direction_output(current_driver->led1, 0);
			gpio_set_value_cansleep(current_driver->led1, 1);
		} else
			i2c_del_driver(&sc628a_i2c_driver);
		rc = gpio_request(current_driver->led2, "sc628a");
		if (!rc) {
			gpio_direction_output(current_driver->led2, 0);
			gpio_set_value_cansleep(current_driver->led2, 1);
		} else {
			i2c_del_driver(&sc628a_i2c_driver);
			gpio_free(current_driver->led1);
		}
	}
	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
		sc628a_i2c_write_b_flash(0x02, 0x0);
		break;
	case MSM_CAMERA_LED_LOW:
		sc628a_i2c_write_b_flash(0x02, 0x06);
		break;
	case MSM_CAMERA_LED_HIGH:
		sc628a_i2c_write_b_flash(0x02, 0x49);
		break;
	default:
		rc = -EFAULT;
		break;
	}
#endif

//                                               
#ifdef CONFIG_MSM_CAMERA_FLASH_LM2759
	{
		rc = msm_camera_flash_lm2759(led_state);
	}
#endif
//                            

#ifdef CONFIG_MSM_CAMERA_FLASH_LM3559
	/*           
                                  
                                   
  */
	rc = msm_camera_flash_lm3559(led_state);
#endif

	return rc;
}


static int msm_camera_flash_pwm(
	struct msm_camera_sensor_flash_pwm *pwm,
	unsigned led_state)
{
	int rc = 0;
	int PWM_PERIOD = USEC_PER_SEC / pwm->freq;

	static struct pwm_device *flash_pwm;

	if (!flash_pwm) {
		flash_pwm = pwm_request(pwm->channel, "camera-flash");
		if (flash_pwm == NULL || IS_ERR(flash_pwm)) {
			pr_err("%s: FAIL pwm_request(): flash_pwm=%p\n",
			       __func__, flash_pwm);
			flash_pwm = NULL;
			return -ENXIO;
		}
	}

	switch (led_state) {
	case MSM_CAMERA_LED_LOW:
		rc = pwm_config(flash_pwm,
			(PWM_PERIOD/pwm->max_load)*pwm->low_load,
			PWM_PERIOD);
		if (rc >= 0)
			rc = pwm_enable(flash_pwm);
		break;

	case MSM_CAMERA_LED_HIGH:
		rc = pwm_config(flash_pwm,
			(PWM_PERIOD/pwm->max_load)*pwm->high_load,
			PWM_PERIOD);
		if (rc >= 0)
			rc = pwm_enable(flash_pwm);
		break;

	case MSM_CAMERA_LED_OFF:
		pwm_disable(flash_pwm);
		break;

	default:
		rc = -EFAULT;
		break;
	}

	return rc;
}

int msm_camera_flash_pmic(
	struct msm_camera_sensor_flash_pmic *pmic,
	unsigned led_state)
{
	int rc = 0;

	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
		rc = pmic->pmic_set_current(pmic->led_src_1, 0);
		if (pmic->num_of_src > 1)
			rc = pmic->pmic_set_current(pmic->led_src_2, 0);
		break;

	case MSM_CAMERA_LED_LOW:
		rc = pmic->pmic_set_current(pmic->led_src_1,
				pmic->low_current);
		if (pmic->num_of_src > 1)
			rc = pmic->pmic_set_current(pmic->led_src_2, 0);
		break;

	case MSM_CAMERA_LED_HIGH:
		rc = pmic->pmic_set_current(pmic->led_src_1,
			pmic->high_current);
		if (pmic->num_of_src > 1)
			rc = pmic->pmic_set_current(pmic->led_src_2,
				pmic->high_current);
		break;

	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);

	return rc;
}

int32_t msm_camera_flash_set_led_state(
	struct msm_camera_sensor_flash_data *fdata, unsigned led_state)
{
	int32_t rc;

	CDBG("flash_set_led_state: %d flash_sr_type=%d\n", led_state,
	    fdata->flash_src->flash_sr_type);

	if (fdata->flash_type != MSM_CAMERA_FLASH_LED)
		return -ENODEV;

	switch (fdata->flash_src->flash_sr_type) {
	case MSM_CAMERA_FLASH_SRC_PMIC:
		rc = msm_camera_flash_pmic(&fdata->flash_src->_fsrc.pmic_src,
			led_state);
		break;

	case MSM_CAMERA_FLASH_SRC_PWM:
		rc = msm_camera_flash_pwm(&fdata->flash_src->_fsrc.pwm_src,
			led_state);
		break;

	case MSM_CAMERA_FLASH_SRC_CURRENT_DRIVER:
		rc = msm_camera_flash_current_driver(
			&fdata->flash_src->_fsrc.current_driver_src,
			led_state);
		break;

	default:
		rc = -ENODEV;
		break;
	}

	return rc;
}

static int msm_strobe_flash_xenon_charge(int32_t flash_charge,
		int32_t charge_enable, uint32_t flash_recharge_duration)
{
	gpio_set_value_cansleep(flash_charge, charge_enable);
	if (charge_enable) {
		timer_flash.expires = jiffies +
			msecs_to_jiffies(flash_recharge_duration);
		/* add timer for the recharge */
		if (!timer_pending(&timer_flash))
			add_timer(&timer_flash);
	} else
		del_timer_sync(&timer_flash);
	return 0;
}

static void strobe_flash_xenon_recharge_handler(unsigned long data)
{
	unsigned long flags;
	struct msm_camera_sensor_strobe_flash_data *sfdata =
		(struct msm_camera_sensor_strobe_flash_data *)data;

	spin_lock_irqsave(&sfdata->timer_lock, flags);
	msm_strobe_flash_xenon_charge(sfdata->flash_charge, 1,
		sfdata->flash_recharge_duration);
	spin_unlock_irqrestore(&sfdata->timer_lock, flags);

	return;
}

static irqreturn_t strobe_flash_charge_ready_irq(int irq_num, void *data)
{
	struct msm_camera_sensor_strobe_flash_data *sfdata =
		(struct msm_camera_sensor_strobe_flash_data *)data;

	/* put the charge signal to low */
	gpio_set_value_cansleep(sfdata->flash_charge, 0);

	return IRQ_HANDLED;
}

static int msm_strobe_flash_xenon_init(
	struct msm_camera_sensor_strobe_flash_data *sfdata)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&sfdata->spin_lock, flags);
	if (!sfdata->state) {

		rc = config_flash_gpio_table(MSM_CAM_FLASH_ON, sfdata);
		if (rc < 0) {
			pr_err("%s: gpio_request failed\n", __func__);
			goto go_out;
		}
		rc = request_irq(sfdata->irq, strobe_flash_charge_ready_irq,
			IRQF_TRIGGER_RISING, "charge_ready", sfdata);
		if (rc < 0) {
			pr_err("%s: request_irq failed %d\n", __func__, rc);
			goto go_out;
		}

		spin_lock_init(&sfdata->timer_lock);
		/* setup timer */
		init_timer(&timer_flash);
		timer_flash.function = strobe_flash_xenon_recharge_handler;
		timer_flash.data = (unsigned long)sfdata;
	}
	sfdata->state++;
go_out:
	spin_unlock_irqrestore(&sfdata->spin_lock, flags);

	return rc;
}

static int msm_strobe_flash_xenon_release
(struct msm_camera_sensor_strobe_flash_data *sfdata, int32_t final_release)
{
	unsigned long flags;

	spin_lock_irqsave(&sfdata->spin_lock, flags);
	if (sfdata->state > 0) {
		if (final_release)
			sfdata->state = 0;
		else
			sfdata->state--;

		if (!sfdata->state) {
			free_irq(sfdata->irq, sfdata);
			config_flash_gpio_table(MSM_CAM_FLASH_OFF, sfdata);
			if (timer_pending(&timer_flash))
				del_timer_sync(&timer_flash);
		}
	}
	spin_unlock_irqrestore(&sfdata->spin_lock, flags);
	return 0;
}

static void msm_strobe_flash_xenon_fn_init
	(struct msm_strobe_flash_ctrl *strobe_flash_ptr)
{
	strobe_flash_ptr->strobe_flash_init =
				msm_strobe_flash_xenon_init;
	strobe_flash_ptr->strobe_flash_charge =
				msm_strobe_flash_xenon_charge;
	strobe_flash_ptr->strobe_flash_release =
				msm_strobe_flash_xenon_release;
}

int msm_strobe_flash_init(struct msm_sync *sync, uint32_t sftype)
{
	int rc = 0;
	switch (sftype) {
	case MSM_CAMERA_STROBE_FLASH_XENON:
		if (sync->sdata->strobe_flash_data) {
			msm_strobe_flash_xenon_fn_init(&sync->sfctrl);
			rc = sync->sfctrl.strobe_flash_init(
			sync->sdata->strobe_flash_data);
		} else
			return -ENODEV;
		break;
	default:
		rc = -ENODEV;
	}
	return rc;
}

int msm_strobe_flash_ctrl(struct msm_camera_sensor_strobe_flash_data *sfdata,
	struct strobe_flash_ctrl_data *strobe_ctrl)
{
	int rc = 0;
	switch (strobe_ctrl->type) {
	case STROBE_FLASH_CTRL_INIT:
		if (!sfdata)
			return -ENODEV;
		rc = msm_strobe_flash_xenon_init(sfdata);
		break;
	case STROBE_FLASH_CTRL_CHARGE:
		rc = msm_strobe_flash_xenon_charge(sfdata->flash_charge,
			strobe_ctrl->charge_en,
			sfdata->flash_recharge_duration);
		break;
	case STROBE_FLASH_CTRL_RELEASE:
		if (sfdata)
			rc = msm_strobe_flash_xenon_release(sfdata, 0);
		break;
	default:
		pr_err("Invalid Strobe Flash State\n");
		rc = -EINVAL;
	}
	return rc;
}

int msm_flash_ctrl(struct msm_camera_sensor_info *sdata,
	struct flash_ctrl_data *flash_info)
{
	int rc = 0;
	switch (flash_info->flashtype) {
	case LED_FLASH:
		rc = msm_camera_flash_set_led_state(sdata->flash_data,
			flash_info->ctrl_data.led_state);
			break;
	case STROBE_FLASH:
		rc = msm_strobe_flash_ctrl(sdata->strobe_flash_data,
			&(flash_info->ctrl_data.strobe_ctrl));
		break;
	default:
		pr_err("Invalid Flash MODE\n");
		rc = -EINVAL;
	}
	return rc;
}
