#include <linux/kernel.h>
#include <mach/msm_hsusb.h>
#include <mach/rpc_hsusb.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <mach/board.h>
#include <mach/board_lge.h>

#include "u_lgeusb.h"

// raw cable info function (comes from Modem side)
unsigned lge_get_cable_info(void);

#define LGE_CABLE_TYPE_MASK                 0x0000000F
#define LGE_CABLE_TYPE_NV_MANUAL_TESTMODE   0x00001000

int lge_is_factory_cable(int *type, int *is_manual)
{
	int udc_cable = lge_get_cable_info();
	int manual_nv = udc_cable & LGE_CABLE_TYPE_NV_MANUAL_TESTMODE;

	udc_cable &= LGE_CABLE_TYPE_MASK;
	if (type != NULL)
		*type = udc_cable;

	if (is_manual != NULL)
		*is_manual = manual_nv ? 1 : 0;

	if (manual_nv)
		return 1;

	return (udc_cable >= LGE_CABLE_TYPE_56K);
}
EXPORT_SYMBOL(lge_is_factory_cable);

