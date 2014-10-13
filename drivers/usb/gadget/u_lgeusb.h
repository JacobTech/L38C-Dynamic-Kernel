#ifndef __U_LGEUSB_H__
#define __U_LGEUSB_H__

// cable detecting
#define LGE_CABLE_TYPE_56K                  0x00000007
#define LGE_CABLE_TYPE_130K                 0x00000008
#define LGE_CABLE_TYPE_910K                 0x00000009

int lge_is_factory_cable(int *type, int *is_manual);

// pid setting
#define LGE_FACTORY_PID 0x6000
#define LGE_DEFAULT_PID 0x61FA
#define LGE_UMSONLY_PID 0x61C5

#define LGE_NO_RESET	0
#define LGE_NEED_RESET	1

void android_lge_switch_driver(int pid, int need_reset);
int android_lge_get_current_pid(void);

#endif /*                */
