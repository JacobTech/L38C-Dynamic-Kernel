#include <linux/err.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>

#include <mach/msm_rpcrouter.h>
#include <mach/rpc_pmapp.h>
#include <mach/board.h>

#include "devices.h"
#include "board-m3eu.h"
#include <mach/board_lge.h>
#include <mach/vreg.h>
// peter_ported start
#include <linux/fb.h>

#define GPIO_LCD_RESET_N 125
#define MDP_303_VSYNC_GPIO 97
#define MSM_FB_LCDC_VREG_OP(name, op, level)	\
	do {														\
		vreg = vreg_get(0, name);								\
		vreg_set_level(vreg, level);							\
		if (vreg_##op(vreg))									\
			printk(KERN_ERR "%s: %s vreg operation failed \n",	\
				(vreg_##op == vreg_enable) ? "vreg_enable"	\
				: "vreg_disable", name);						\
	} while (0)
// peter_ported end
/* backlight device */
static struct gpio_i2c_pin bl_i2c_pin = {
	.sda_pin = 112,
	.scl_pin = 111,
	.reset_pin = 124,
};

static struct i2c_gpio_platform_data bl_i2c_pdata = {
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.udelay = 2,
};

static struct platform_device bl_i2c_device = {
	.name = "i2c-gpio",
	.dev.platform_data = &bl_i2c_pdata,
};

#ifdef CONFIG_BACKLIGHT_AAT2870 // peter_ported
static struct lge_backlight_platform_data lm3530bl_data = {
	.gpio = 124,
	.version = 3530,
static struct i2c_board_info bl_i2c_bdinfo[] = {
	[0] = {
		I2C_BOARD_INFO("aat2870bl", 0x60),
		.type = "aat2870bl",
	},
};
#endif

#ifdef CONFIG_BACKLIGHT_BU61800 //peter_ported
static struct lge_backlight_platform_data bu61800bl_data = {
	.gpio = 124,
	.version = 61800,
};

static struct i2c_board_info bl_i2c_bdinfo[] = {
	[0] = {
		I2C_BOARD_INFO("bu61800bl", 0x76),
		.type = "bu61800bl",
	},
};
#endif

/* peter_ported
static struct platform_device mipi_dsi_r61529_panel_device = {
	.name = "mipi_r61529",

	.id = 0,
};
*/
// peter_ported start
static int ebi2_tovis_power_save(int on);
static struct msm_panel_ilitek_pdata ebi2_tovis_panel_data = {
	.gpio = GPIO_LCD_RESET_N,
	.lcd_power_save = ebi2_tovis_power_save,
	.maker_id = PANEL_ID_TOVIS,
	.initialized = 1,
};

static struct platform_device ebi2_tovis_panel_device = {
	.name	= "ebi2_tovis_qvga",
	.id 	= 0,
	.dev	= {
		.platform_data = &ebi2_tovis_panel_data,
	}
};
// peter_ported end
/* input platform device */
static struct platform_device *m3eu_panel_devices[] __initdata = {
	&ebi2_tovis_panel_device,
//	&mipi_dsi_r61529_panel_device, peter_ported
};

static struct msm_panel_common_pdata mdp_pdata = {
	.gpio = MDP_303_VSYNC_GPIO,			//LCD_VSYNC_O
//	.gpio = 97,						//LCD_VSYNC_O peter_ported
	.mdp_rev = MDP_REV_303,
};
static char *msm_fb_vreg[] = {
	"wlan_tcx0",
	"emmc",
};
/*
enum {
	DSI_SINGLE_LANE = 1,
	DSI_TWO_LANES,
};

static int msm_fb_get_lane_config(void)
{
	int rc = DSI_TWO_LANES;
#if 0
	if (cpu_is_msm7x25a()) {
		rc = DSI_SINGLE_LANE;
		pr_info("DSI Single Lane\n");
	} else {
		pr_info("DSI Two Lanes\n");
	}
#endif
	return rc;
}
*/
static int mddi_power_save_on;
static int ebi2_tovis_power_save(int on)
{
	struct vreg *vreg;
	int flag_on = !!on;

	printk(KERN_INFO "%s: on=%d\n", __func__, flag_on);

	if (mddi_power_save_on == flag_on)
		return 0;

	mddi_power_save_on = flag_on;

	if (on) {
		//MSM_FB_LCDC_VREG_OP(msm_fb_vreg[0], enable, 1800);
		MSM_FB_LCDC_VREG_OP(msm_fb_vreg[1], enable, 2800);	
	} else{
		//MSM_FB_LCDC_VREG_OP(msm_fb_vreg[0], disable, 0);
		MSM_FB_LCDC_VREG_OP(msm_fb_vreg[1], disable, 0);
		}
	return 0;
		}

static int m3trf_fb_event_notify(struct notifier_block *self,
	unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	struct fb_var_screeninfo *var = &info->var;
	if(action == FB_EVENT_FB_REGISTERED) {
		var->width = 43;
		var->height = 58;
	}
	return 0;
}
static struct notifier_block e0eu_fb_event_notifier = {
	.notifier_call	= m3tfr_fb_event_notify,
};
static void __init msm_fb_add_devices(void)
{
	msm_fb_register_device("mdp", &mdp_pdata);
	msm_fb_register_device("lcdc", 0);
	msm_fb_register_device("ebi2", 0);
}
#ifdef CONFIG_BACKLIGHT_BU61800
void __init msm7x27a_e0eu_init_i2c_backlight(int bus_num)
{
	bl_i2c_device.id = bus_num;
	bl_i2c_bdinfo[0].platform_data = &bu61800bl_data;

	/* workaround for HDK rev_a no pullup */
	lge_init_gpio_i2c_pin_pullup(&bl_i2c_pdata, bl_i2c_pin, &bl_i2c_bdinfo[0]);
	i2c_register_board_info(bus_num, &bl_i2c_bdinfo[0], 1);
	platform_device_register(&bl_i2c_device);
}
#endif
/*                                                              */


void __init lge_add_lcd_devices(void)
{
        if(ebi2_tovis_panel_data.initialized)
		ebi2_tovis_power_save(1);

	fb_register_client(&e0eu_fb_event_notifier);    

	platform_add_devices(e0eu_panel_devices, ARRAY_SIZE(e0eu_panel_devices));
	msm_fb_add_devices();
	lge_add_gpio_i2c_device(msm7x27a_e0eu_init_i2c_backlight);
}
#if 0 /* peter_ported */
#define GPIO_LCD_RESET 125
static int dsi_gpio_initialized;

static int mipi_dsi_panel_power(int on)
{
	int rc = 0;
	struct vreg *vreg_mipi_dsi_v28;

	printk("mipi_dsi_panel_power : %d \n",on);

	if (!dsi_gpio_initialized) {

		// Resetting LCD Panel
		rc = gpio_request(GPIO_LCD_RESET, "lcd_reset");
		if (rc) {
			pr_err("%s: gpio_request GPIO_LCD_RESET failed\n", __func__);
		}
		rc = gpio_tlmm_config(GPIO_CFG(GPIO_LCD_RESET, 0, GPIO_CFG_OUTPUT,
				GPIO_CFG_NO_PULL, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
		if (rc){
			printk(KERN_ERR "%s: Failed to configure GPIO %d\n",
					__func__, rc);
		}

		dsi_gpio_initialized = 1;
	}

	vreg_mipi_dsi_v28 = vreg_get(0, "emmc");
	if (IS_ERR(vreg_mipi_dsi_v28)) {
		pr_err("%s: vreg_get for emmc failed\n", __func__);
		return PTR_ERR(vreg_mipi_dsi_v28);
	}

	if (on) {
		rc = vreg_set_level(vreg_mipi_dsi_v28, 2800); 
		if (rc) {
			pr_err("%s: vreg_set_level failed for mipi_dsi_v28\n", __func__);
			goto vreg_put_dsi_v28;
		}
		rc = vreg_enable(vreg_mipi_dsi_v28); 
		if (rc) {
			pr_err("%s: vreg_enable failed for mipi_dsi_v28\n", __func__);
			goto vreg_put_dsi_v28;
		}

		rc = gpio_direction_output(GPIO_LCD_RESET, 1);
		if (rc) {
			pr_err("%s: gpio_direction_output failed for lcd_reset\n", __func__);
			goto vreg_put_dsi_v28;
		}

		mdelay(10);
		gpio_set_value(GPIO_LCD_RESET, 0);
		mdelay(10);
		gpio_set_value(GPIO_LCD_RESET, 1);
		mdelay(10);		
	} else {
		rc = vreg_disable(vreg_mipi_dsi_v28);
		if (rc) {
			pr_err("%s: vreg_disable failed for mipi_dsi_v28\n", __func__);
			goto vreg_put_dsi_v28;
		}
	}

vreg_put_dsi_v28:
	vreg_put(vreg_mipi_dsi_v28);

	return rc;
}

#define MDP_303_VSYNC_GPIO 97

#ifdef CONFIG_FB_MSM_MDP303
static struct mipi_dsi_platform_data mipi_dsi_pdata = {
	.vsync_gpio = MDP_303_VSYNC_GPIO,
	.dsi_power_save   = mipi_dsi_panel_power,
#ifndef CONFIG_MACH_LGE
	.dsi_client_reset = msm_fb_dsi_client_reset,
#endif
	.get_lane_config = msm_fb_get_lane_config,
};
#endif

static void __init msm_fb_add_devices(void)
{
	msm_fb_register_device("mdp", &mdp_pdata);
	msm_fb_register_device("lcdc", 0);
	msm_fb_register_device("mipi_dsi", &mipi_dsi_pdata);
}

void __init msm7x27a_m3eu_init_i2c_backlight(int bus_num)
{
	bl_i2c_device.id = bus_num;
	bl_i2c_bdinfo[0].platform_data = &lm3530bl_data;

//                                                                    
if (lge_bd_rev == EVB) { // [M3D] For EVB bl GPIO changed , M3DVIV, M3DOPEN
		bl_i2c_pin.scl_pin = 122;
		bl_i2c_pin.sda_pin = 123;
	}
//                                                                  
	/* workaround for HDK rev_a no pullup */
	lge_init_gpio_i2c_pin_pullup(&bl_i2c_pdata, bl_i2c_pin, &bl_i2c_bdinfo[0]);
	i2c_register_board_info(bus_num, &bl_i2c_bdinfo[0], 1);
	platform_device_register(&bl_i2c_device);
}

void __init lge_add_lcd_devices(void)
{
	platform_add_devices(m3eu_panel_devices, ARRAY_SIZE(m3eu_panel_devices));
	msm_fb_add_devices();
	lge_add_gpio_i2c_device(msm7x27a_m3eu_init_i2c_backlight);
}
#endif
