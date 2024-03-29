#include <linux/module.h>
#include <lg_diagcmd.h>
#include <linux/input.h>
#include <linux/syscalls.h>

#include <lg_fw_diag_communication.h>
#include <lg_diag_testmode.h>
#include <mach/qdsp5v2/audio_def.h>
#include <linux/delay.h>

#ifndef SKW_TEST
#include <linux/fcntl.h> 
#include <linux/fs.h>
#include <linux/uaccess.h>
#endif

#include <linux/crypto.h>
#include <crypto/hash.h>

#include <../../kernel/arch/arm/mach-msm/smd_private.h>
#include <linux/slab.h>

//                                                            
#include <linux/parser.h>
//                                                            

#include <mach/board_lge.h>
#include <lg_backup_items.h>

#include <linux/gpio.h>
#include <linux/mfd/pmic8058.h>
#include <mach/irqs.h>

#if 0 // M3 use Internal SD, not External SD
// m3 use Internal SD, so we dont use this
#else
#define SYS_GPIO_SD_DET 40
#endif
#define PM8058_GPIO_BASE NR_MSM_GPIOS
#define PM8058_GPIO_PM_TO_SYS(pm_gpio) (pm_gpio + PM8058_GPIO_BASE)
//                                                            
#define WL_IS_WITHIN(min,max,expr)         (((min)<=(expr))&&((max)>(expr)))
//                                                            

static struct diagcmd_dev *diagpdev;

extern PACK(void *) diagpkt_alloc (diagpkt_cmd_code_type code, unsigned int length);
extern PACK(void *) diagpkt_free (PACK(void *)pkt);
extern void send_to_arm9( void * pReq, void * pRsp);
extern testmode_user_table_entry_type testmode_mstr_tbl[TESTMODE_MSTR_TBL_SIZE];
extern int diag_event_log_start(void);
extern int diag_event_log_end(void);
extern void set_operation_mode(boolean isOnline);
extern struct input_dev* get_ats_input_dev(void);

#ifdef CONFIG_LGE_DIAG_KEYPRESS
extern unsigned int LGF_KeycodeTrans(word input);
extern void LGF_SendKey(word keycode);
#endif

extern int boot_info;
extern void *smem_alloc(unsigned id, unsigned size);

static int disable_check=0;
/* ==========================================================================
===========================================================================*/

struct statfs_local {
 __u32 f_type;
 __u32 f_bsize;
 __u32 f_blocks;
 __u32 f_bfree;
 __u32 f_bavail;
 __u32 f_files;
 __u32 f_ffree;
 __kernel_fsid_t f_fsid;
 __u32 f_namelen;
 __u32 f_frsize;
 __u32 f_spare[5];
};

/* ==========================================================================
===========================================================================*/

extern int lge_bd_rev;

//                                                            
typedef struct _rx_packet_info 
{
	int goodpacket;
	int badpacket;
} rx_packet_info_t;

enum {
	Param_none = -1,
	Param_goodpacket,
	Param_badpacket,
	Param_end,
	Param_err,
};

static const match_table_t param_tokens = {
	{Param_goodpacket, "good=%d"},
	{Param_badpacket, "bad=%d"},
	{Param_end,	"END"},
	{Param_err, NULL}
};
//                                                            


void CheckHWRev(byte *pStr)
{
    char *rev_str[] = {"evb", "A", "B", "C", "D",
        "E", "F", "G", "1.0", "1.1", "1.2",
        "revserved"};

    strcpy((char *)pStr ,(char *)rev_str[lge_bd_rev]);
}

PACK (void *)LGF_TestMode (
        PACK (void	*)req_pkt_ptr, /* pointer to request packet */
        uint16 pkt_len )        /* length of request packet */
{
    DIAG_TEST_MODE_F_req_type *req_ptr = (DIAG_TEST_MODE_F_req_type *) req_pkt_ptr;
    DIAG_TEST_MODE_F_rsp_type *rsp_ptr;
    unsigned int rsp_len=0;
    testmode_func_type func_ptr= NULL;
    int nIndex = 0;

    diagpdev = diagcmd_get_dev();

    // DIAG_TEST_MODE_F_rsp_type union type is greater than the actual size, decrease it in case sensitive items
    switch(req_ptr->sub_cmd_code)
    {
        case TEST_MODE_FACTORY_RESET_CHECK_TEST:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type);
            break;

        case TEST_MODE_TEST_SCRIPT_MODE:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_req_test_script_mode_type);
            break;

        //REMOVE UNNECESSARY RESPONSE PACKET FOR EXTERNEL SOCKET ERASE
        case TEST_MODE_EXT_SOCKET_TEST:
            if((req_ptr->test_mode_req.esm == EXTERNAL_SOCKET_ERASE) || (req_ptr->test_mode_req.esm == EXTERNAL_SOCKET_ERASE_SDCARD_ONLY) \
                    || (req_ptr->test_mode_req.esm == EXTERNAL_SOCKET_ERASE_FAT_ONLY) || (req_ptr->test_mode_req.esm == INTERNAL_SD_MEMORY_ERASE))
                rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type);
            else
                rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type);
            break;

        //Added by jaeopark 110527 for XO Cal Backup
        case TEST_MODE_XO_CAL_DATA_COPY:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_req_XOCalDataBackup_Type);
            break;

        case TEST_MODE_MANUAL_TEST_MODE:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_req_manual_test_mode_type);
            break;

        case TEST_MODE_BLUETOOTH_RW:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_req_bt_addr_type);
            break;

        case TEST_MODE_WIFI_MAC_RW:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_req_wifi_addr_type);
            break;

/*                                                                                       */
#if 1 //                                                       
        case TEST_MODE_SLEEP_MODE_TEST:
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type) - sizeof(test_mode_rsp_type) + sizeof(test_mode_sleep_mode_type);
            break;
#endif

        default :
            rsp_len = sizeof(DIAG_TEST_MODE_F_rsp_type);
            break;
    }

    rsp_ptr = (DIAG_TEST_MODE_F_rsp_type *)diagpkt_alloc(DIAG_TEST_MODE_F, rsp_len);

    printk(KERN_ERR "[LGF_TestMode] rsp_len: %d, sub_cmd_code: %d \n", rsp_len, req_ptr->sub_cmd_code);

    if (!rsp_ptr)
        return 0;

    rsp_ptr->sub_cmd_code = req_ptr->sub_cmd_code;
    rsp_ptr->ret_stat_code = TEST_OK_S; // test ok

    for( nIndex = 0 ; nIndex < TESTMODE_MSTR_TBL_SIZE  ; nIndex++)
    {
        if( testmode_mstr_tbl[nIndex].cmd_code == req_ptr->sub_cmd_code)
        {
            if( testmode_mstr_tbl[nIndex].which_procesor == ARM11_PROCESSOR)
                func_ptr = testmode_mstr_tbl[nIndex].func_ptr;
            break;
        }
    }
    
    printk(KERN_ERR "[LGF_TestMode] testmode_mstr_tbl Index : %d \n", nIndex);

    if( func_ptr != NULL)
    {
    		printk(KERN_ERR "[LGF_TestMode] inner if(func_ptr!=NULL) \n");
        return func_ptr( &(req_ptr->test_mode_req), rsp_ptr);
    }
    else
    {
        if(req_ptr->test_mode_req.version == VER_HW)
        {
        		printk(KERN_ERR "[LGF_TestMode] inner if(req_ptr->test_mode_req.version == VER_HW) \n");
            CheckHWRev((byte *)rsp_ptr->test_mode_rsp.str_buf);
        }
        else
            send_to_arm9((void*)req_ptr, (void*)rsp_ptr);
    }

    return (rsp_ptr);
}
EXPORT_SYMBOL(LGF_TestMode);

void* linux_app_handler(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    diagpkt_free(pRsp);
    return 0;
}

void* not_supported_command_handler(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
    return pRsp;
}

char external_memory_copy_test(void)
{
    char return_value = TEST_FAIL_S;
    char *src = (void *)0;
    char *dest = (void *)0;
    off_t fd_offset;
    int fd;
    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

    if ( (fd = sys_open((const char __user *) "/sdcard/_ExternalSD/SDTest.txt", O_CREAT | O_RDWR, 0) ) < 0 )
    {
        printk(KERN_ERR "[Testmode Memory Test] Can not access SD card\n");
        goto file_fail;
    }

    if ( (src = (char*) kmalloc(10, GFP_KERNEL)) )
    {
        sprintf(src,"TEST");
        if ((sys_write(fd, (const char __user *) src, 5)) < 0)
        {
            printk(KERN_ERR "[Testmode Memory Test] Can not write SD card \n");
            goto file_fail;
        }

        fd_offset = sys_lseek(fd, 0, 0);
    }

    if ( (dest = (char*) kmalloc(10, GFP_KERNEL)) )
    {
        if ((sys_read(fd, (char __user *) dest, 5)) < 0)
        {
            printk(KERN_ERR "[Testmode Memory Test] Can not read SD card \n");
            goto file_fail;
        }

        if ((memcmp(src, dest, 4)) == 0)
            return_value = TEST_OK_S;
        else
            return_value = TEST_FAIL_S;
    }

    kfree(src);
    kfree(dest);

file_fail:
    sys_close(fd);
    set_fs(old_fs);
    sys_unlink((const char __user *)"/sdcard/_ExternalSD/SDTest.txt");

    return return_value;
}

extern int external_memory_test_diag;

bool LGF_IsInternalSDRequest(test_mode_req_type * pReq)
{
	switch( pReq->esm){
		case INTERNAL_SD_MEMORY_ERASE:
			return true;
		case EXTERNAL_SOCKET_MEMORY_CHECK:
		case EXTERNAL_FLASH_MEMORY_SIZE:
		case EXTERNAL_SOCKET_ERASE:
		case EXTERNAL_FLASH_MEMORY_USED_SIZE:
		case EXTERNAL_FLASH_MEMORY_CONTENTS_CHECK:
		case EXTERNAL_FLASH_MEMORY_ERASE:
		case EXTERNAL_SOCKET_ERASE_SDCARD_ONLY:
		case EXTERNAL_SOCKET_ERASE_FAT_ONLY:
		default:
			return false;
	}
	return false;
}

void* LGF_ExternalSocketMemory(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    struct statfs_local sf;
    pRsp->ret_stat_code = TEST_FAIL_S;

    // ADD: 0013541: 0014142: [Test_Mode] To remove Internal memory information in External memory test when SD-card is not exist
#if 0
// m3 use Internal SD, so we dont use this
#else
//    if(gpio_get_value(PM8058_GPIO_PM_TO_SYS(PMIC_GPIO_SDC3_DET - 1)))
	if(!LGF_IsInternalSDRequest(pReq)){
	    if(gpio_get_value(SYS_GPIO_SD_DET)) //dy.lee
	    {
	        if (pReq->esm == EXTERNAL_SOCKET_MEMORY_CHECK)
	        {
	            pRsp->test_mode_rsp.memory_check = TEST_FAIL_S;
	            pRsp->ret_stat_code = TEST_OK_S;
	        }
	        
	        printk(KERN_ERR "[Testmode Memory Test] Can not detect SD card\n");
	        return pRsp;
	    }
    }
#endif

    switch( pReq->esm){
        case EXTERNAL_SOCKET_MEMORY_CHECK:
            pRsp->test_mode_rsp.memory_check = external_memory_copy_test();
            pRsp->ret_stat_code = TEST_OK_S;
            break;

        case EXTERNAL_FLASH_MEMORY_SIZE:
            if (sys_statfs("/sdcard/_ExternalSD", (struct statfs *)&sf) != 0)
            {
                printk(KERN_ERR "[Testmode Memory Test] can not get sdcard infomation \n");
                break;
            }

            pRsp->test_mode_rsp.socket_memory_size = ((long long)sf.f_blocks * (long long)sf.f_bsize) >> 20; // needs Mb.
            pRsp->ret_stat_code = TEST_OK_S;
            break;

        case EXTERNAL_SOCKET_ERASE:
            if (diagpdev == NULL){
                  diagpdev = diagcmd_get_dev();
                  printk("\n[%s] diagpdev is Null", __func__ );
            }
            
            if (diagpdev != NULL)
            {
                update_diagcmd_state(diagpdev, "MMCFORMAT", 1);
                msleep(5000);
                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                printk("\n[%s] error FACTORY_RESET", __func__ );
                pRsp->ret_stat_code = TEST_FAIL_S;
            }
            break;

        case EXTERNAL_FLASH_MEMORY_USED_SIZE:
            external_memory_test_diag = -1;
            update_diagcmd_state(diagpdev, "CALCUSEDSIZE", 0);
            msleep(1000);

            if(external_memory_test_diag != -1)
            {
                pRsp->test_mode_rsp.socket_memory_usedsize = external_memory_test_diag;
                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                pRsp->ret_stat_code = TEST_FAIL_S;
                printk(KERN_ERR "[CALCUSEDSIZE] DiagCommandObserver returned fail or didn't return in 100ms.\n");
            }

            break;

        case EXTERNAL_FLASH_MEMORY_CONTENTS_CHECK:
            external_memory_test_diag = -1;
            update_diagcmd_state(diagpdev, "CHECKCONTENTS", 0);
            msleep(1000);
            
            if(external_memory_test_diag != -1)
            {
                if(external_memory_test_diag == 1)
                    pRsp->test_mode_rsp.memory_check = TEST_OK_S;
                else 
                    pRsp->test_mode_rsp.memory_check = TEST_FAIL_S;

                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                pRsp->ret_stat_code = TEST_FAIL_S;
                printk(KERN_ERR "[CHECKCONTENTS] DiagCommandObserver returned fail or didn't return in 1000ms.\n");
            }
            
            break;

        case EXTERNAL_FLASH_MEMORY_ERASE:
            external_memory_test_diag = -1;
            update_diagcmd_state(diagpdev, "ERASEMEMORY", 0);
            msleep(5000);
            
            if(external_memory_test_diag != -1)
            {
                if(external_memory_test_diag == 1)
                    pRsp->test_mode_rsp.memory_check = TEST_OK_S;
                else
                    pRsp->test_mode_rsp.memory_check = TEST_FAIL_S;

                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                pRsp->ret_stat_code = TEST_FAIL_S;
                printk(KERN_ERR "[ERASEMEMORY] DiagCommandObserver returned fail or didn't return in 5000ms.\n");
            }
            
            break;
		case INTERNAL_SD_MEMORY_ERASE:
			if (diagpdev == NULL){
				  diagpdev = diagcmd_get_dev();
				  printk("\n[%s] diagpdev is Null", __func__ );
			}
			
			if (diagpdev != NULL)
			{
				update_diagcmd_state(diagpdev, "MMCFORMAT", 2);
				msleep(5000);
				pRsp->ret_stat_code = TEST_OK_S;
			}
			else
			{
				printk("\n[%s] error FACTORY_RESET", __func__ );
				pRsp->ret_stat_code = TEST_FAIL_S;
			}
			break;

        case EXTERNAL_SOCKET_ERASE_SDCARD_ONLY: /*0xE*/
            if (diagpdev != NULL)
            {
                update_diagcmd_state(diagpdev, "MMCFORMAT", EXTERNAL_SOCKET_ERASE_SDCARD_ONLY);
                msleep(5000);
                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                printk("\n[%s] error EXTERNAL_SOCKET_ERASE_SDCARD_ONLY", __func__ );
                pRsp->ret_stat_code = TEST_FAIL_S;
            }
            break;

        case EXTERNAL_SOCKET_ERASE_FAT_ONLY: /*0xF*/
            if (diagpdev != NULL)
            {
                update_diagcmd_state(diagpdev, "MMCFORMAT", EXTERNAL_SOCKET_ERASE_FAT_ONLY);
                msleep(5000);
                pRsp->ret_stat_code = TEST_OK_S;
            }
            else
            {
                printk("\n[%s] error EXTERNAL_SOCKET_ERASE_FAT_ONLY", __func__ );
                pRsp->ret_stat_code = TEST_FAIL_S;
            }
            break;

        default:
            pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
            break;
    }

    return pRsp;
}

void* LGF_TestModeBattLevel(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
//                                             

	DIAG_TEST_MODE_F_req_type req_ptr;

  	req_ptr.sub_cmd_code = TEST_MODE_BATT_LEVEL_TEST;
  	req_ptr.test_mode_req.batt = pReq->batt;

 	pRsp->ret_stat_code = TEST_FAIL_S;

//                                   
#if 0
    int battery_soc = 0;
    extern int max17040_get_battery_capacity_percent(void);

    pRsp->ret_stat_code = TEST_OK_S;

    printk(KERN_ERR "%s, pRsp->ret_stat_code : %d\n", __func__, pReq->batt);
    if(pReq->batt == BATTERY_FUEL_GAUGE_SOC_NPST)
    {
        battery_soc = (int)max17040_get_battery_capacity_percent();
    }
    else
    {
        pRsp->ret_stat_code = TEST_FAIL_S;
    }

    if(battery_soc > 100)
        battery_soc = 100;
    else if (battery_soc < 0)
        battery_soc = 0;

    printk(KERN_ERR "%s, battery_soc : %d\n", __func__, battery_soc);

    sprintf((char *)pRsp->test_mode_rsp.batt_voltage, "%d", battery_soc);

    printk(KERN_ERR "%s, battery_soc : %s\n", __func__, (char *)pRsp->test_mode_rsp.batt_voltage);
#endif

	send_to_arm9((void*)&req_ptr, (void*)pRsp);
	//printk(KERN_INFO "%s, result : %s\n", __func__, pRsp->ret_stat_code==TEST_OK_S?"OK":"FALSE");

	if(pRsp->ret_stat_code == TEST_FAIL_S)
	{
		printk(KERN_ERR "[Testmode]send_to_arm9 response : %d\n", pRsp->ret_stat_code);
		pRsp->ret_stat_code = TEST_FAIL_S;
	}
	else if(pRsp->ret_stat_code == TEST_OK_S)
	{
	  	printk(KERN_ERR "[Testmode]send_to_arm9 response : %d\n", pRsp->ret_stat_code);
        pRsp->ret_stat_code = TEST_OK_S;
	}
//                                             
    return pRsp;
}

void* LGF_TestModeKeyData(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{

    pRsp->ret_stat_code = TEST_OK_S;

#ifdef CONFIG_LGE_DIAG_KEYPRESS
    LGF_SendKey(LGF_KeycodeTrans(pReq->key_data));
#endif

    return pRsp;
}


/*                                                                                       */
#ifdef CONFIG_LGE_DIAG_DISABLE_INPUT_DEVICES_ON_SLEEP_MODE 
static int test_mode_disable_input_devices = 0;
void LGF_TestModeSetDisableInputDevices(int value)
{
    test_mode_disable_input_devices = value;
}
int LGF_TestModeGetDisableInputDevices(void)
{
    return test_mode_disable_input_devices;
}
EXPORT_SYMBOL(LGF_TestModeGetDisableInputDevices);
#endif

extern void LGF_SendKey(word keycode);
extern int rt9396_get_state(void);

static boolean testMode_sleepMode = FALSE;
boolean LGF_TestMode_Is_SleepMode(void)
{
	return testMode_sleepMode;
}
EXPORT_SYMBOL(LGF_TestMode_Is_SleepMode);
//                                                 
// minimum current at SMT
void LGF_SetTestMode(boolean b) {
	testMode_sleepMode = b;
}
EXPORT_SYMBOL(LGF_SetTestMode);
//                                              

void* LGF_TestModeSleepMode(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
#if 1
	DIAG_TEST_MODE_F_req_type req_ptr;

  	req_ptr.sub_cmd_code = TEST_MODE_SLEEP_MODE_TEST;

	pRsp->ret_stat_code = TEST_OK_S;
	req_ptr.test_mode_req.sleep_mode = (pReq->sleep_mode & 0x00FF); 	// 2011.06.21 biglake for power test after cal

	switch(req_ptr.test_mode_req.sleep_mode){	
		case SLEEP_MODE_ON:
            if(rt9396_get_state() == 1 /* NORMAL_STATE */)
			    LGF_SendKey(KEY_POWER);
            
            //send_to_arm9((void*)&req_ptr, (void*)pRsp);
            testMode_sleepMode = FALSE;
			break;            
		case FLIGHT_MODE_ON:
	  	case FLIGHT_KERNEL_MODE_ON:                        
		//                                                 
		// minimum current at SMT
		{
			testMode_sleepMode = TRUE;

			LGF_SendKey(KEY_POWER);
            //send_to_arm9((void*)&req_ptr, (void*)pRsp);
		}
		//                                              
			break;
		case FLIGHT_MODE_OFF:            
            if(rt9396_get_state() == 2 /* SLEEP_STATE */)
			    LGF_SendKey(KEY_POWER);
            
            set_operation_mode(TRUE);
            testMode_sleepMode = FALSE;			
            //send_to_arm9((void*)&req_ptr, (void*)pRsp);           
			break;
		default:
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
            testMode_sleepMode = FALSE;			
            break;
	}
    #if 0
	switch(pReq->sleep_mode){	
		case SLEEP_MODE_ON:
			LGF_SendKey(KEY_POWER);
			break;		
		case FLIGHT_MODE_ON:
			LGF_SendKey(KEY_POWER);
			//if_condition_is_on_air_plain_mode = 1;
			set_operation_mode(FALSE);
			break;
	  	case FLIGHT_KERNEL_MODE_ON:
			break;
		case FLIGHT_MODE_OFF:
			set_operation_mode(TRUE);
			break;
		default:
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
            break;
	}
    #endif

	return pRsp;
#else
    pRsp->ret_stat_code = TEST_FAIL_S;

#ifdef CONFIG_LGE_DIAG_DISABLE_INPUT_DEVICES_ON_SLEEP_MODE 
    switch(pReq->sleep_mode)
    {
        /* ignore touch, key events on this mode */
        case SLEEP_MODE_ON:
            printk(KERN_INFO "%s, disable input devices..\n", __func__);
            LGF_TestModeSetDisableInputDevices(1);
            pRsp->ret_stat_code = TEST_OK_S;
            break;

        case FLIGHT_MODE_ON:
            break;            
        case FLIGHT_KERNEL_MODE_ON:
            break;
        case FLIGHT_MODE_OFF:
            break;

        default:
            break;
    }
#endif

    return pRsp;
#endif
}

void* LGF_TestModeVirtualSimTest(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{

    pRsp->ret_stat_code = TEST_OK_S;
    return pRsp;
}

//extern int boot_info;

void* LGF_TestModeFBoot(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    switch( pReq->fboot)
    {
        case FIRST_BOOTING_COMPLETE_CHECK:
            if (boot_info)
                pRsp->ret_stat_code = TEST_OK_S;
            else
                pRsp->ret_stat_code = TEST_FAIL_S;
            break;

#if 0
        case FIRST_BOOTING_CHG_MODE_CHECK:
            if(get_first_booting_chg_mode_status() == 1)
                pRsp->ret_stat_code = FIRST_BOOTING_IN_CHG_MODE;
            else
                pRsp->ret_stat_code = FIRST_BOOTING_NOT_IN_CHG_MODE;
            break;
#endif

        default:
            pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
            break;
    }
    return pRsp;
}

extern int db_integrity_ready;
extern int fpri_crc_ready;
extern int file_crc_ready;
extern int db_dump_ready;
extern int db_copy_ready;

typedef struct {
    char ret[32];
} testmode_rsp_from_diag_type;

extern testmode_rsp_from_diag_type integrity_ret;
void* LGF_TestModeDBIntegrityCheck(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    unsigned int crc_val;

    memset(integrity_ret.ret, 0, 32);

    if (diagpdev != NULL)
    {
        update_diagcmd_state(diagpdev, "DBCHECK", pReq->db_check);

        switch(pReq->db_check)
        {
            case DB_INTEGRITY_CHECK:
                while ( !db_integrity_ready )
                    msleep(10);

                db_integrity_ready = 0;

                msleep(100); // wait until the return value is written to the file

                crc_val = (unsigned int)simple_strtoul(integrity_ret.ret+1,NULL,16);
                sprintf(pRsp->test_mode_rsp.str_buf, "0x%08X", crc_val);

                printk(KERN_INFO "%s\n", integrity_ret.ret);
                printk(KERN_INFO "%d\n", crc_val);
                printk(KERN_INFO "%s\n", pRsp->test_mode_rsp.str_buf);

                pRsp->ret_stat_code = TEST_OK_S;
                break;

            case FPRI_CRC_CHECK:
                while ( !fpri_crc_ready )
                    msleep(10);

                fpri_crc_ready = 0;

                msleep(100); // wait until the return value is written to the file

                crc_val = (unsigned int)simple_strtoul(integrity_ret.ret+1,NULL,16);
                sprintf(pRsp->test_mode_rsp.str_buf, "0x%08X", crc_val);

                printk(KERN_INFO "%s\n", integrity_ret.ret);
                printk(KERN_INFO "%d\n", crc_val);
                printk(KERN_INFO "%s\n", pRsp->test_mode_rsp.str_buf);

                pRsp->ret_stat_code = TEST_OK_S;
                break;

            case FILE_CRC_CHECK:
                while ( !file_crc_ready )
                    msleep(10);

                file_crc_ready = 0;

                msleep(100); // wait until the return value is written to the file

                crc_val = (unsigned int)simple_strtoul(integrity_ret.ret+1,NULL,16);
                sprintf(pRsp->test_mode_rsp.str_buf, "0x%08X", crc_val);

                printk(KERN_INFO "%s\n", integrity_ret.ret);
                printk(KERN_INFO "%d\n", crc_val);
                printk(KERN_INFO "%s\n", pRsp->test_mode_rsp.str_buf);

                pRsp->ret_stat_code = TEST_OK_S;
                break;

            case CODE_PARTITION_CRC_CHECK:
                pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
                break;

            case TOTAL_CRC_CHECK:
                pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
                break;

            case DB_DUMP_CHECK:
                while ( !db_dump_ready )
                    msleep(10);

                db_dump_ready = 0;

                msleep(100); // wait until the return value is written to the file

                if (integrity_ret.ret[0] == '0')
                    pRsp->ret_stat_code = TEST_OK_S;
                else
                    pRsp->ret_stat_code = TEST_FAIL_S;

                break;

            case DB_COPY_CHECK:
                while ( !db_copy_ready )
                    msleep(10);

                db_copy_ready = 0;

                msleep(100); // wait until the return value is written to the file

                if (integrity_ret.ret[0] == '0')
                    pRsp->ret_stat_code = TEST_OK_S;
                else
                    pRsp->ret_stat_code = TEST_FAIL_S;

                break;

            default :
                pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
                break;
        }
    }
    else
    {
        printk("\n[%s] error DBCHECK", __func__ );
        pRsp->ret_stat_code = TEST_FAIL_S;
    }

    printk(KERN_ERR "[_DBCHECK_] [%s:%d] DBCHECK Result=<%s>\n", __func__, __LINE__, integrity_ret.ret);

    return pRsp;
}

//                                                 
 #define fota_id_length 15
void* LGF_TestModeFotaIDCheck(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
	int fd = -1;
	int i = 0;
	char fota_id_read[fota_id_length] = {0,};
	char *src = (void *)0;	
	mm_segment_t old_fs=get_fs();
    set_fs(get_ds());
	
    if (diagpdev != NULL)
    {
        switch( pReq->fota_id_check)
        {
            case FOTA_ID_CHECK:
                update_diagcmd_state(diagpdev, "FOTAIDCHECK", 0);
                msleep(500);

				if ( (fd = sys_open((const char __user *) "/sys/module/lge_emmc_direct_access/parameters/fota_id_check", O_CREAT | O_RDWR, 0777) ) < 0 )
			    {
			    	printk(KERN_ERR "[FOTA_TEST_MODE] Can not open file .\n");
					pRsp->ret_stat_code = TEST_FAIL_S;
					goto fota_fail;
			    }
				if ( (src = kmalloc(20, GFP_KERNEL)) )
				{
					if ((sys_read(fd, (char __user *) src, 2)) < 0)
					{
						printk(KERN_ERR "[FOTA_TEST_MODE] Can not read file.\n");
						pRsp->ret_stat_code = TEST_FAIL_S;
						goto fota_fail;
					}
			        if ((memcmp(src, "0", 1)) == 0)
			        {
			        	kfree(src);
			        	sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_check");	
			       		pRsp->ret_stat_code = TEST_OK_S;
						printk(KERN_ERR "[##LMH_TEST] TEST_OK \n");	
						return pRsp; 
			        }	
			        else
			        {
			        	kfree(src);
						sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_check");	
			       		pRsp->ret_stat_code = TEST_FAIL_S;
						printk(KERN_ERR "[##LMH_TEST] TEST_FAIL \n");	
						return pRsp;
			        }	
				}
				
                break;
				
            case FOTA_ID_READ:
                update_diagcmd_state(diagpdev, "FOTAIDREAD", 0);
                msleep(500);

				if ( (fd = sys_open((const char __user *) "/sys/module/lge_emmc_direct_access/parameters/fota_id_read", O_CREAT | O_RDWR, 0777) ) < 0 )
			    {
			    	printk(KERN_ERR "[FOTA_TEST_MODE] Can not open file .\n");
					pRsp->ret_stat_code = TEST_FAIL_S;
					goto fota_fail;
			    }
				printk(KERN_ERR "[##LMH_TEST] fota_id_check is %s \n", fota_id_read);

				{
					if (sys_read(fd, (char __user *) fota_id_read, fota_id_length-1) < 0)
					{
						printk(KERN_ERR "[FOTA_TEST_MODE] Can not read file.\n");
						pRsp->ret_stat_code = TEST_FAIL_S;
						goto fota_fail;
					}
			        if ((memcmp((void*)fota_id_read, "fail", 4)) != 0)	//f is fail, and f is not 0x
			        {

			        	sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_read");	
						printk(KERN_ERR "[##LMH_TEST] fota_id_check is %s \n", fota_id_read);
			       		pRsp->ret_stat_code = TEST_OK_S;
		
						for(i=0;i<fota_id_length-1;i++){
							pRsp->test_mode_rsp.fota_id[i] = fota_id_read[i];
							printk(KERN_ERR "[##LMH_TEST] fota_id_check is %d \n", fota_id_read[i]);
						}
						printk(KERN_ERR "[##LMH_TEST] TEST_OK \n");	
						return pRsp; 
			        }	
				   
			        else
			        {
						sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_read");	
			       		pRsp->ret_stat_code = TEST_FAIL_S;
						printk(KERN_ERR "[##LMH_TEST] TEST_FAIL \n");	
						return pRsp;
			        }	
			        
				}
				
                break;

            default:
                pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
                break;
        }
    }
    else
        pRsp->ret_stat_code = TEST_FAIL_S;

fota_fail:
		kfree(src);
		sys_close(fd);
		set_fs(old_fs); 
		sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_check");
		sys_unlink((const char __user *)"/sys/module/lge_emmc_direct_access/parameters/fota_id_read");

    return pRsp;
}
//                                                 

//                                                            
static char wifi_get_rx_packet_info(rx_packet_info_t* rx_info)
{
	const char* src = "/data/misc/wifi/diag_wifi_result";
	char return_value = TEST_FAIL_S;
	char *dest = (void *)0;
	char buf[30];
	off_t fd_offset;
	int fd;
	char *tok, *holder = NULL;
	char *delimiter = ":\r\n";
	substring_t args[MAX_OPT_ARGS];	
	int token;	
	char tmpstr[10];

    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

	if (rx_info == NULL) {
		goto file_fail;
	}
	
	memset(buf, 0x00, sizeof(buf));

    if ( (fd = sys_open((const char __user *)src, O_CREAT | O_RDWR, 0) ) < 0 )
    {
        printk(KERN_ERR "[Testmode Wi-Fi] sys_open() failed!!\n");
        goto file_fail;
    }

    if ( (dest = kmalloc(30, GFP_KERNEL)) )
    {
        fd_offset = sys_lseek(fd, 0, 0);

        if ((sys_read(fd, (char __user *) dest, 30)) < 0)
        {
            printk(KERN_ERR "[Testmode Wi-Fi] can't read path %s \n", src);
            goto file_fail;
        }

		if ((memcmp(dest, "30", 2)) == 0) {
			printk(KERN_INFO "rx_packet_cnt read error \n");
			goto file_fail;
		}

		strncpy(buf, (const char *)dest, sizeof(buf) - 1);
		buf[sizeof(buf)-1] = 0;
		holder = &(buf[2]); // skip index, result
		
		while (holder != NULL) {
			tok = strsep(&holder, delimiter);
			
			if (!*tok)
				continue;

			token = match_token(tok, param_tokens, args);
			switch (token) {
			case Param_goodpacket:
				memset(tmpstr, 0x00, sizeof(tmpstr));
				if (0 == match_strlcpy(tmpstr, &args[0], sizeof(tmpstr)))
				{
					printk(KERN_ERR "Error GoodPacket %s", args[0].from);
					continue;
				}
				rx_info->goodpacket = (int)simple_strtol(tmpstr, NULL, 0);
				printk(KERN_INFO "[Testmode Wi-Fi] rx_info->goodpacket = %d", rx_info->goodpacket);
				break;

			case Param_badpacket:
				memset(tmpstr, 0x00, sizeof(tmpstr));
				if (0 == match_strlcpy(tmpstr, &args[0], sizeof(tmpstr)))
				{
					printk(KERN_ERR "Error BadPacket %s\n", args[0].from);
					continue;
				}

				rx_info->badpacket = (int)simple_strtol(tmpstr, NULL, 0);
				printk(KERN_INFO "[Testmode Wi-Fi] rx_info->badpacket = %d", rx_info->badpacket);
				return_value = TEST_OK_S;
				break;

			case Param_end:
			case Param_err:
			default:
				/* silently ignore unknown settings */
				printk(KERN_ERR "[Testmode Wi-Fi] ignore unknown token %s\n", tok);
				break;
			}
		}
    }

	printk(KERN_INFO "[Testmode Wi-Fi] return_value %d!!\n", return_value);
	
file_fail:    
    kfree(dest);
    sys_close(fd);
    sys_unlink((const char __user *)src);
    set_fs(old_fs);
    return return_value;
}


static char wifi_get_test_results(int index)
{
    const char* src = "/data/misc/wifi/diag_wifi_result";
    char return_value = TEST_FAIL_S;
    char *dest = (void *)0;
    char buf[4]={0};
    off_t fd_offset;
    int fd;
    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

    if ( (fd = sys_open((const char __user *)src, O_CREAT | O_RDWR, 0) ) < 0 )
    {
        printk(KERN_ERR "[Testmode Wi-Fi] sys_open() failed!!\n");
        goto file_fail;
    }

    if ( (dest = kmalloc(20, GFP_KERNEL)) )
    {
        fd_offset = sys_lseek(fd, 0, 0);

        if ((sys_read(fd, (char __user *) dest, 20)) < 0)
        {
            printk(KERN_ERR "[Testmode Wi-Fi] can't read path %s \n", src);
            goto file_fail;
        }

        sprintf(buf, "%d""1", index);
        buf[2]='\0';
        printk(KERN_INFO "[Testmode Wi-Fi] result %s!!\n", buf);

        if ((memcmp(dest, buf, 2)) == 0)
            return_value = TEST_OK_S;
        else
            return_value = TEST_FAIL_S;
		
        printk(KERN_ERR "[Testmode Wi-Fi] return_value %d!!\n", return_value);

    }
	
file_fail:
    kfree(dest);
    sys_close(fd);
    sys_unlink((const char __user *)src);
    set_fs(old_fs);

    return return_value;
}


static test_mode_ret_wifi_ctgry_t divide_into_wifi_category(test_mode_req_wifi_type input)
{
	test_mode_ret_wifi_ctgry_t sub_category = WLAN_TEST_MODE_CTGRY_NOT_SUPPORTED;
	
	if ( input == WLAN_TEST_MODE_54G_ON || 
		WL_IS_WITHIN(WLAN_TEST_MODE_11B_ON, WLAN_TEST_MODE_11A_CH_RX_START, input)) {
		sub_category = WLAN_TEST_MODE_CTGRY_ON;
	} else if ( input == WLAN_TEST_MODE_OFF ) {
		sub_category = WLAN_TEST_MODE_CTGRY_OFF;
	} else if ( input == WLAN_TEST_MODE_RX_RESULT ) {
		sub_category = WLAN_TEST_MODE_CTGRY_RX_STOP;
	} else if ( WL_IS_WITHIN(WLAN_TEST_MODE_RX_START, WLAN_TEST_MODE_RX_RESULT, input) || 
			WL_IS_WITHIN(WLAN_TEST_MODE_LF_RX_START, WLAN_TEST_MODE_MF_TX_START, input)) {
        sub_category = WLAN_TEST_MODE_CTGRY_RX_START;
	} else if ( WL_IS_WITHIN(WLAN_TEST_MODE_TX_START, WLAN_TEST_MODE_TXRX_STOP, input) || 
			WL_IS_WITHIN( WLAN_TEST_MODE_MF_TX_START, WLAN_TEST_MODE_11B_ON, input)) {
		sub_category = WLAN_TEST_MODE_CTGRY_TX_START;
	} else if ( input == WLAN_TEST_MODE_TXRX_STOP) {
		sub_category = WLAN_TEST_MODE_CTGRY_TX_STOP;
	}
	
	printk(KERN_INFO "[divide_into_wifi_category] input = %d, sub_category = %d!!\n", input, sub_category );
	
	return sub_category;	
}


void* LGF_TestModeWLAN(
        test_mode_req_type*	pReq,
        DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	int i;
	static int first_on_try = 10;
	test_mode_ret_wifi_ctgry_t wl_category;

	if (diagpdev != NULL)
	{
		update_diagcmd_state(diagpdev, "WIFI_TEST_MODE", pReq->wifi);

		printk(KERN_ERR "[WI-FI] [%s:%d] WiFiSubCmd=<%d>\n", __func__, __LINE__, pReq->wifi);

		wl_category = divide_into_wifi_category(pReq->wifi);

		/* Set Test Mode */
		switch (wl_category) {

			case WLAN_TEST_MODE_CTGRY_ON:
				//[10sec timeout] when wifi turns on, it takes about 9seconds to bring up FTM mode.
				for (i = 0; i< first_on_try ; i++) {
					msleep(1000);
				}

				first_on_try = 5;

				pRsp->ret_stat_code = wifi_get_test_results(wl_category);
				pRsp->test_mode_rsp.wlan_status = !(pRsp->ret_stat_code);
				break;

			case WLAN_TEST_MODE_CTGRY_OFF:
				//5sec timeout
				for (i = 0; i< 5; i++)
					msleep(1000);
				pRsp->ret_stat_code = wifi_get_test_results(wl_category);
				break;

			case WLAN_TEST_MODE_CTGRY_RX_START:
				for (i = 0; i< 2; i++)
					msleep(1000);
				pRsp->ret_stat_code = wifi_get_test_results(wl_category);
				pRsp->test_mode_rsp.wlan_status = !(pRsp->ret_stat_code);
				break;

			case WLAN_TEST_MODE_CTGRY_RX_STOP:
			{
				rx_packet_info_t rx_info;
				int total_packet = 0;
				int m_rx_per = 0;
				// init
				rx_info.goodpacket = 0;
				rx_info.badpacket = 0;
				// wait 4 secs
				for (i = 0; i< 4; i++)
					msleep(1000);
				
				pRsp->test_mode_rsp.wlan_rx_results.packet = 0;
				pRsp->test_mode_rsp.wlan_rx_results.per = 0;

				pRsp->ret_stat_code = wifi_get_rx_packet_info(&rx_info);
				if (pRsp->ret_stat_code == TEST_OK_S) {
					total_packet = rx_info.badpacket + rx_info.goodpacket;
					if(total_packet > 0) {
						m_rx_per = (rx_info.badpacket * 1000 / total_packet);
						printk(KERN_INFO "[WI-FI] per = %d, rx_info.goodpacket = %d, rx_info.badpacket = %d ",
							m_rx_per, rx_info.goodpacket, rx_info.badpacket);
					}
					pRsp->test_mode_rsp.wlan_rx_results.packet = rx_info.goodpacket;
					pRsp->test_mode_rsp.wlan_rx_results.per = m_rx_per;
				}
				break;
			}

			case WLAN_TEST_MODE_CTGRY_TX_START:
				for (i = 0; i< 2; i++)
					msleep(1000);
				pRsp->ret_stat_code = wifi_get_test_results(wl_category);
				pRsp->test_mode_rsp.wlan_status = !(pRsp->ret_stat_code);
				break;

			case WLAN_TEST_MODE_CTGRY_TX_STOP:
				for (i = 0; i< 2; i++)
					msleep(1000);
				pRsp->ret_stat_code = wifi_get_test_results(wl_category);
				pRsp->test_mode_rsp.wlan_status = !(pRsp->ret_stat_code);
				break;

			default:
				pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
				break;
		}
	}
	else
	{
		printk(KERN_ERR "[WI-FI] [%s:%d] diagpdev %d ERROR\n", __func__, __LINE__, pReq->wifi);
		pRsp->ret_stat_code = TEST_FAIL_S;
	}

	return pRsp;
}

//                                                            



//                                                                         
void* LGF_TestModeWiFiMACRW(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
//                                                            
	DIAG_TEST_MODE_F_req_type req_ptr;

	req_ptr.sub_cmd_code = TEST_MODE_WIFI_MAC_RW;
	printk(KERN_ERR "[LGF_TestModeWiFiMACRW] req_type=%d, wifi_mac_addr=[%s]\n", pReq->wifi_mac_ad.req_type, pReq->wifi_mac_ad.wifi_mac_addr);

	if (diagpdev != NULL)
	{
		pRsp->ret_stat_code = TEST_FAIL_S;
		if( pReq->wifi_mac_ad.req_type == 0) {
			req_ptr.test_mode_req.wifi_mac_ad.req_type = 0;
			memcpy(req_ptr.test_mode_req.wifi_mac_ad.wifi_mac_addr, (void*)(pReq->wifi_mac_ad.wifi_mac_addr), WIFI_MAC_ADDR_CNT);
			send_to_arm9((void*)&req_ptr, (void*)pRsp);
			printk(KERN_INFO "[Wi-Fi] %s, result : %s\n", __func__, pRsp->ret_stat_code==TEST_OK_S?"OK":"FAILURE");
		} else if ( pReq->wifi_mac_ad.req_type == 1) {
			req_ptr.test_mode_req.wifi_mac_ad.req_type = 1;
			send_to_arm9((void*)&req_ptr, (void*)pRsp);
			printk(KERN_INFO "[Wi-Fi] %s, result : %s\n", __func__, pRsp->ret_stat_code==TEST_OK_S?"OK":"FAILURE");
		}
		else{
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		}
	}
	else
	{
		printk(KERN_ERR "[WI-FI] [%s:%d] diagpdev %d ERROR\n", __func__, __LINE__, pReq->wifi_mac_ad.req_type);
		pRsp->ret_stat_code = TEST_FAIL_S;
	}

	return pRsp;
//                                                            
#if 0
	int fd=0; 
	int i=0;
    char *src = (void *)0;	
    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

	printk(KERN_ERR "[LGF_TestModeWiFiMACRW] req_type=%d, wifi_mac_addr=[%s]\n", pReq->wifi_mac_ad.req_type, pReq->wifi_mac_ad.wifi_mac_addr);

	if (diagpdev != NULL)
	{
		if( pReq->wifi_mac_ad.req_type == 0 )
		{
			printk(KERN_ERR "[LGF_TestModeWiFiMACRW] WIFI_MAC_ADDRESS_WRITE.\n");
			
			if ( (fd = sys_open((const char __user *) "/data/misc/wifi/diag_mac", O_CREAT | O_RDWR, 0777) ) < 0 )
		    {
		    	printk(KERN_ERR "[LGF_TestModeWiFiMACRW] Can not open file.\n");
				pRsp->ret_stat_code = TEST_FAIL_S;
				goto file_fail;
		    }
				
			if ( (src = kmalloc(20, GFP_KERNEL)) )
			{
				sprintf( src,"%c%c%c%c%c%c%c%c%c%c%c%c", pReq->wifi_mac_ad.wifi_mac_addr[0],
					pReq->wifi_mac_ad.wifi_mac_addr[1], pReq->wifi_mac_ad.wifi_mac_addr[2],
					pReq->wifi_mac_ad.wifi_mac_addr[3], pReq->wifi_mac_ad.wifi_mac_addr[4],
					pReq->wifi_mac_ad.wifi_mac_addr[5], pReq->wifi_mac_ad.wifi_mac_addr[6],
					pReq->wifi_mac_ad.wifi_mac_addr[7], pReq->wifi_mac_ad.wifi_mac_addr[8],
					pReq->wifi_mac_ad.wifi_mac_addr[9], pReq->wifi_mac_ad.wifi_mac_addr[10],
					pReq->wifi_mac_ad.wifi_mac_addr[11]
					);
					
				if ((sys_write(fd, (const char __user *) src, 12)) < 0)
				{
					printk(KERN_ERR "[LGF_TestModeWiFiMACRW] Can not write file.\n");
					pRsp->ret_stat_code = TEST_FAIL_S;
					goto file_fail;
				}
			}

			msleep(500);
				
			update_diagcmd_state(diagpdev, "WIFIMACWRITE", 0);
				
			pRsp->ret_stat_code = TEST_OK_S;

		}
		else if(  pReq->wifi_mac_ad.req_type == 1 )
		{
			printk(KERN_ERR "[LGF_TestModeWiFiMACRW] WIFI_MAC_ADDRESS_READ.\n");
			
			update_diagcmd_state(diagpdev, "WIFIMACREAD", 0);

			for( i=0; i< 2; i++ )
			{
				msleep(500);
			}					

			if ( (fd = sys_open((const char __user *) "/data/misc/wifi/diag_mac", O_CREAT | O_RDWR, 0777) ) < 0 )
		    {
		    	printk(KERN_ERR "[LGF_TestModeWiFiMACRW] Can not open file.\n");
				pRsp->ret_stat_code = TEST_FAIL_S;
				goto file_fail;
		    }
			
			if ( (src = kmalloc(20, GFP_KERNEL)) )
			{
				if ((sys_read(fd, (char __user *) src, 12)) < 0)
				{
					printk(KERN_ERR "[LGF_TestModeWiFiMACRW] Can not read file.\n");
					pRsp->ret_stat_code = TEST_FAIL_S;
					goto file_fail;
				}
			}

			for( i=0; i<14; i++)
			{
				pRsp->test_mode_rsp.key_pressed_buf[i] = 0;
			}

			for( i=0; i< 12; i++ )
			{
				pRsp->test_mode_rsp.read_wifi_mac_addr[i] = src[i];
			}

			sys_unlink((const char __user *)"/data/misc/wifi/diag_mac");
					
			pRsp->ret_stat_code = TEST_OK_S;
		}				
		else
		{
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		}
	}
	else
	{
		pRsp->ret_stat_code = TEST_FAIL_S;
	}

file_fail:
	kfree(src);
	
	sys_close(fd);
	set_fs(old_fs); 
	
	return pRsp;
#endif
}
//                                                                         



#ifndef SKW_TEST
//                                                                
//static unsigned char test_mode_factory_reset_status = FACTORY_RESET_START;
//                                      
#define BUF_PAGE_SIZE 2048
//                                              
// MOD 0010090: [FactoryReset] Enable Recovery mode FactoryReset

#define FACTORY_RESET_STR       "FACT_RESET_"
#define FACTORY_RESET_STR_SIZE	11
#define FACTORY_RESET_BLK 1 // read / write on the first block

#define MSLEEP_CNT 100

typedef struct MmcPartition MmcPartition;

struct MmcPartition {
    char *device_index;
    char *filesystem;
    char *name;
    unsigned dstatus;
    unsigned dtype ;
    unsigned dfirstsec;
    unsigned dsize;
};
//                                            
#endif

//                                                     
void* LGF_TestModeBlueTooth(
        test_mode_req_type*	pReq,
        DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
    int fd;
	char *src = (void *)0;		
    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

	printk(KERN_ERR "[_BTUI_] [%s:%d] BTSubCmd=<%d>\n", __func__, __LINE__, pReq->bt);

	if (diagpdev != NULL)
	{
		update_diagcmd_state(diagpdev, "BT_TEST_MODE", pReq->bt);

		printk(KERN_ERR "[_BTUI_] [%s:%d] BTSubCmd=<%d>\n", __func__, __LINE__, pReq->bt);

		/* Set Test Mode */
		if(pReq->bt==1 || (pReq->bt>=11 && pReq->bt<=42)) 
		{			
			msleep(5900); //6sec timeout
		}
		/*Test Mode Check*/
		else if(pReq->bt==2) 
		{
			ssleep(1);
			if ( (fd = sys_open((const char __user *) "/data/bt_dut_test.txt", O_CREAT | O_RDWR, 0777) ) < 0 )
		    {
		    	printk(KERN_ERR "[BT_TEST_MODE] Can not open file .\n");
				pRsp->ret_stat_code = TEST_FAIL_S;
				goto file_fail;
		    }
			if ( (src = kmalloc(20, GFP_KERNEL)) )
			{
				if ((sys_read(fd, (char __user *) src, 3)) < 0)
				{
					printk(KERN_ERR "[BT_TEST_MODE] Can not read file.\n");
					pRsp->ret_stat_code = TEST_FAIL_S;
					goto file_fail;
				}
		        if ((memcmp(src, "on", 2)) == 0)
		        {
		        	kfree(src);
		        	sys_unlink((const char __user *)"/data/bt_dut_test.txt");	
		       		pRsp->ret_stat_code = TEST_OK_S;
					printk(KERN_ERR "[##LMH_TEST] TEST_OK \n");	
					return pRsp; 
		        }	
		        else
		        {
		        	kfree(src);
					sys_unlink((const char __user *)"/data/bt_dut_test.txt");	
		       		pRsp->ret_stat_code = TEST_FAIL_S;
					printk(KERN_ERR "[##LMH_TEST] TEST_FAIL \n");	
					return pRsp;
		        }	
			}		
		}	
		/*Test Mode Release*/
		else if(pReq->bt==5)
		{
			ssleep(3);
		}
		/*Test Mode Not Supported*/
		else
		{
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
			return pRsp;
		}

		pRsp->ret_stat_code = TEST_OK_S;
		return pRsp;
	}
	else
	{
		printk(KERN_ERR "[_BTUI_] [%s:%d] BTSubCmd=<%d> ERROR\n", __func__, __LINE__, pReq->bt);
		pRsp->ret_stat_code = TEST_FAIL_S;
		return pRsp;
	}  
file_fail:
	kfree(src);
	
	sys_close(fd);
	set_fs(old_fs); 
	sys_unlink((const char __user *)"/data/bt_dut_test.txt");	
	return pRsp;	
}
// +e LG_BTUI_DIAGCMD_DUTMODE

/*                                                                   */
void* LGF_TestMotor(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev != NULL){
		update_diagcmd_state(diagpdev, "MOTOR", pReq->motor);
	}
	else
	{
		printk("\n[%s] error MOTOR", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
	}
	return pRsp;
}

void* LGF_TestLCD(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	char temp[32];
	test_mode_req_mft_lcd_type* mft_lcd = &pReq->mft_lcd;
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev == NULL) {
		pr_info("[%s] error LCD\n", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		return pRsp;
	}

	pr_info(" *** pReq->lcd: %d\n", mft_lcd->type_subcmd);
	switch (mft_lcd->type_subcmd)
	{
	case LCD_GET_INFO:
		sprintf(pRsp->test_mode_rsp.lcd_getinfo, "320,480,24");
		break;

	case LCD_DISPLAY_CHART:
		pr_info(" data[] = { %d (%c) }\n", mft_lcd->data_chart, mft_lcd->data_chart);
		sprintf(temp, "LCD,%c", mft_lcd->data_chart);
		update_diagcmd_state(diagpdev, temp, mft_lcd->type_subcmd);
		break;

	case LCD_DISLPAY_PATTERN_CHART:
		pr_info(" data[] = { %d, %d, %d } -> { %c, %c, %c }\n",
			mft_lcd->data_pattern[0], mft_lcd->data_pattern[1], mft_lcd->data_pattern[2],
			mft_lcd->data_pattern[0], mft_lcd->data_pattern[1], mft_lcd->data_pattern[2]);
		sprintf(temp, "LCD,%c,%c,%c",
			mft_lcd->data_pattern[0], mft_lcd->data_pattern[1], mft_lcd->data_pattern[2]);
		update_diagcmd_state(diagpdev, temp, mft_lcd->type_subcmd);
		break;

	case LCD_GET:
		pr_info(" data[] = { %d, %d, %d, %d }\n",
			mft_lcd->data_lcd_get[0], mft_lcd->data_lcd_get[1], mft_lcd->data_lcd_get[2], mft_lcd->data_lcd_get[3]);
		sprintf(temp, "LCD,%d,%d,%d,%d",
			mft_lcd->data_lcd_get[0], mft_lcd->data_lcd_get[1], mft_lcd->data_lcd_get[2], mft_lcd->data_lcd_get[3]);
		update_diagcmd_state(diagpdev, temp, mft_lcd->type_subcmd);
		pRsp->test_mode_rsp.lcd_get = mft_lcd->data_lcd_get[2]*mft_lcd->data_lcd_get[3]*3;
		break;

	case LCD_INITIAL:
	case LCD_ON:
	case LCD_OFF:
		update_diagcmd_state(diagpdev, "LCD", mft_lcd->type_subcmd);
		break;

	default:
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		return pRsp;
	}
	return pRsp;
}

void* LGF_TestLCD_Cal(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	char ptr[30];

	if (diagpdev == NULL) {
		printk("\n[%s] error LCD_cal", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		return pRsp;
	}

	pRsp->ret_stat_code = TEST_OK_S;
	printk("<6>" "pReq->lcd_cal:%d, (%x,%x)\n", pReq->lcd_cal, pReq->MaxRGB[0], pReq->MaxRGB[1]);

	if (pReq->MaxRGB[0] != 5) {
		update_diagcmd_state(diagpdev, "LCD_Cal", pReq->MaxRGB[0]);
	}
	else {
		printk("<6>" "pReq->MaxRGB string type : %s\n",pReq->MaxRGB);
		sprintf(ptr,"LCD_Cal,%s",&pReq->MaxRGB[1]);
		printk("<6>" "%s \n", ptr);
		update_diagcmd_state(diagpdev, ptr, pReq->MaxRGB[0]);
	}

	return pRsp;
}



static int test_mode_disable_input_devices = 0;
void LGF_TestModeSetDisableInputDevices(int value)
{
    test_mode_disable_input_devices = value;
}
int LGF_TestModeGetDisableInputDevices(void)
{
    return test_mode_disable_input_devices;
}
EXPORT_SYMBOL(LGF_TestModeGetDisableInputDevices);
void* LGF_TestModeKeyLockUnlock(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	DIAG_TEST_MODE_F_req_type req_ptr;
	
	req_ptr.sub_cmd_code =TEST_MODE_KEY_LOCK_UNLOCK;
	pRsp->ret_stat_code = TEST_OK_S;
	req_ptr.test_mode_req.key_lock_unlock=pReq->key_lock_unlock;
	
	if (diagpdev == NULL) {
		pr_info("[%s] error Key lock unlock\n", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		return pRsp;
	}
	 
	switch (req_ptr.test_mode_req.key_lock_unlock)
	{
		case 0://key lock
			if(disable_check ==0){
				LGF_TestModeSetDisableInputDevices(1);
				update_diagcmd_state(diagpdev, "KEYLOCK",req_ptr.test_mode_req.key_lock_unlock);
				//disable_touch_key(0);
				disable_check=1;
			}
			break;
		case 1:
			if(disable_check ==1){
				LGF_TestModeSetDisableInputDevices(0);
				update_diagcmd_state(diagpdev, "KEYLOCK",req_ptr.test_mode_req.key_lock_unlock);
				//disable_touch_key(1);
				disable_check=0;
			}
			break;
		case 2:
			pRsp->test_mode_rsp.key_check=disable_check;
			break;
		default:
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
			return pRsp;
	}
	return pRsp;
}
void* LGF_TestAcoustic(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev != NULL){
		update_diagcmd_state(diagpdev, "ACOUSTIC", pReq->acoustic);
	}
	else
	{
		printk("\n[%s] error ACOUSTIC", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
	}
	return pRsp;
}

byte key_buf[MAX_KEY_BUFF_SIZE];
int count_key_buf = 0;

boolean lgf_factor_key_test_rsp (char key_code)
{
	/* sanity check */
	if (count_key_buf>=MAX_KEY_BUFF_SIZE)
		return FALSE;

	key_buf[count_key_buf++] = key_code;
	return TRUE;
}
EXPORT_SYMBOL(lgf_factor_key_test_rsp);
void* LGT_TestModeKeyTest(test_mode_req_type* pReq, DIAG_TEST_MODE_F_rsp_type *pRsp)
{
  pRsp->ret_stat_code = TEST_OK_S;

  if(pReq->key_test_start){
	memset((void *)key_buf,0x00,MAX_KEY_BUFF_SIZE);
	count_key_buf=0;
	diag_event_log_start();
  }
  else
  {
	memcpy((void *)((DIAG_TEST_MODE_KEY_F_rsp_type *)pRsp)->key_pressed_buf, (void *)key_buf, MAX_KEY_BUFF_SIZE);
	memset((void *)key_buf,0x00,MAX_KEY_BUFF_SIZE);
	diag_event_log_end();
  }  
  return pRsp;
}
void* LGF_TestCam(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	switch(pReq->camera)
	{
		case CAM_TEST_CAMERA_SELECT:
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
			break;

		default:
			if (diagpdev != NULL){

				update_diagcmd_state(diagpdev, "CAMERA", pReq->camera);
			}
			else
			{
				printk("\n[%s] error CAMERA", __func__ );
				pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
			}
			break;
	}
	return pRsp;
}

void* LGF_TestModeMP3 (
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev != NULL){
		if(pReq->mp3_play == MP3_SAMPLE_FILE)
		{
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		}
		else
		{
			update_diagcmd_state(diagpdev, "MP3", pReq->mp3_play);
		}
	}
	else
	{
		printk("\n[%s] error MP3", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
	}
	return pRsp;
}

void* LGF_MemoryVolumeCheck (
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	struct statfs_local  sf;
	unsigned int total = 0;
	unsigned int used = 0;
	unsigned int remained = 0;
	pRsp->ret_stat_code = TEST_OK_S;

	if (sys_statfs("/data", (struct statfs *)&sf) != 0)
	{
		printk(KERN_ERR "[Testmode]can not get sdcard infomation \n");
		pRsp->ret_stat_code = TEST_FAIL_S;
	}
	else
	{

		total = (sf.f_blocks * sf.f_bsize) >> 20;
		remained = (sf.f_bavail * sf.f_bsize) >> 20;
		used = total - remained;

		switch(pReq->mem_capa)
		{
			case MEMORY_TOTAL_CAPA_TEST:
				pRsp->test_mode_rsp.mem_capa = total;
				break;

			case MEMORY_USED_CAPA_TEST:
				pRsp->test_mode_rsp.mem_capa = used;
				break;

			case MEMORY_REMAIN_CAPA_TEST:
				pRsp->test_mode_rsp.mem_capa = remained;
				break;

			default :
				pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
				break;
		}
	}
	return pRsp;
}

void* LGF_TestModeSpeakerPhone(
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type	*pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev != NULL){
		if((pReq->speaker_phone == NOMAL_Mic1) || (pReq->speaker_phone == NC_MODE_ON)
				|| (pReq->speaker_phone == ONLY_MIC2_ON_NC_ON) || (pReq->speaker_phone == ONLY_MIC1_ON_NC_ON)
		  )
		{
			pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		}
		else
		{
			update_diagcmd_state(diagpdev, "SPEAKERPHONE", pReq->speaker_phone);
		}
	}
	else
	{
		printk("\n[%s] error SPEAKERPHONE", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
	}
	return pRsp;
}

void* LGT_TestModeVolumeLevel (
		test_mode_req_type* pReq ,
		DIAG_TEST_MODE_F_rsp_type *pRsp)
{
	pRsp->ret_stat_code = TEST_OK_S;

	if (diagpdev != NULL){
		update_diagcmd_state(diagpdev, "VOLUMELEVEL", pReq->volume_level);
	}
	else
	{
		printk("\n[%s] error VOLUMELEVEL", __func__ );
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
	}
	return pRsp;
}
/*                                                                   */

// emmc interface
int lge_crack_internal_sdcard(int blockCnt);
int lge_get_frst_flag(void);
int lge_set_frst_flag(int flag);

void* LGF_TestModeFactoryReset(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
	DIAG_TEST_MODE_F_req_type req_ptr;

	req_ptr.sub_cmd_code = TEST_MODE_FACTORY_RESET_CHECK_TEST;
	req_ptr.test_mode_req.factory_reset = pReq->factory_reset;
	pRsp->ret_stat_code = TEST_FAIL_S;

	switch(pReq->factory_reset)
	{
	case FACTORY_RESET_CHECK :
		send_to_arm9((void*)&req_ptr, (void*)pRsp);
		pr_info("[Testmode]send_to_arm9 response : %d\n", pRsp->ret_stat_code);

		if (pRsp->ret_stat_code == TEST_OK_S)
			lge_set_frst_flag(2);
		break;

	case FACTORY_RESET_COMPLETE_CHECK:
		send_to_arm9((void*)&req_ptr, (void*)pRsp);
		pr_info("[Testmode]send_to_arm9 response : %d\n", pRsp->ret_stat_code);
		break;

	case FACTORY_RESET_STATUS_CHECK:
		pRsp->ret_stat_code = lge_get_frst_flag();
		if (pRsp->ret_stat_code == 4)
			pRsp->ret_stat_code = 3;
		break;

	case FACTORY_RESET_COLD_BOOT:
		lge_crack_internal_sdcard(10);
		lge_set_frst_flag(3);
		pRsp->ret_stat_code = TEST_OK_S;
		break;

	default:
		pRsp->ret_stat_code = TEST_NOT_SUPPORTED_S;
		break;
	}

	return pRsp;
}
//                                                          
int HiddenMenu_FactoryReset(void)
{
	DIAG_TEST_MODE_F_req_type req_ptr;
	DIAG_TEST_MODE_F_rsp_type rsp_ptr; 

	//NV_INIT
	req_ptr.sub_cmd_code = TEST_MODE_FACTORY_RESET_CHECK_TEST;
	req_ptr.test_mode_req.factory_reset = FACTORY_RESET_CHECK;
	send_to_arm9((void*)&req_ptr, (void*)&rsp_ptr);
	
    if (rsp_ptr.ret_stat_code != TEST_OK_S)
		return -1;

	lge_set_frst_flag(2);
	req_ptr.test_mode_req.factory_reset = FACTORY_RESET_COMPLETE_CHECK;
	send_to_arm9((void*)&req_ptr, (void*)&rsp_ptr);	

    if (rsp_ptr.ret_stat_code != TEST_OK_S)
		return -1;

    //COLD_BOOT		
	lge_set_frst_flag(3);
	return 0;
}
//                                                          
void* LGF_TestScriptItemSet(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
	DIAG_TEST_MODE_F_req_type req_ptr;

	req_ptr.sub_cmd_code = TEST_MODE_TEST_SCRIPT_MODE;
	req_ptr.test_mode_req.test_mode_test_scr_mode = pReq->test_mode_test_scr_mode;

	switch(pReq->test_mode_test_scr_mode)
	{
	case TEST_SCRIPT_ITEM_SET:
		send_to_arm9((void*)&req_ptr, (void*)pRsp);
		pr_info("%s, result : %s\n", __func__, pRsp->ret_stat_code==TEST_OK_S ? "OK" : "FALSE");

		if (pRsp->ret_stat_code == TEST_OK_S)
			lge_set_frst_flag(0);
		break;

	default:
		send_to_arm9((void*)&req_ptr, (void*)pRsp);
		printk(KERN_INFO "%s, cmd : %d, result : %s\n", __func__, pReq->test_mode_test_scr_mode, \
				pRsp->ret_stat_code==TEST_OK_S?"OK":"FALSE");
		if(pReq->test_mode_test_scr_mode == TEST_SCRIPT_MODE_CHECK)
		{
			switch(pRsp->test_mode_rsp.test_mode_test_scr_mode)
			{
			case 0:
				printk(KERN_INFO "%s, mode : %s\n", __func__, "USER SCRIPT");
				break;

			case 1:
				printk(KERN_INFO "%s, mode : %s\n", __func__, "TEST SCRIPT");
				break;

			default:
				printk(KERN_INFO "%s, mode : %s, returned %d\n", __func__, "NO PRL", pRsp->test_mode_rsp.test_mode_test_scr_mode);
				break;
			}
		}
		break;
	}  

	return pRsp;
}

//                                   
void* LGF_TestModeMLTEnableSet(test_mode_req_type * pReq, DIAG_TEST_MODE_F_rsp_type * pRsp)
{
    char *src = (void *)0;
    char *dest = (void *)0;
    off_t fd_offset;
    int fd;

    mm_segment_t old_fs=get_fs();
    set_fs(get_ds());

    pRsp->ret_stat_code = TEST_FAIL_S;

    if (diagpdev != NULL)
    {
        if ( (fd = sys_open((const char __user *) "/mpt/enable", O_CREAT | O_RDWR, 0) ) < 0 )
        {
            printk(KERN_ERR "[Testmode MPT] Can not access MPT\n");
            goto file_fail;
        }

		if ( (src = kmalloc(5, GFP_KERNEL)) )
		{
			sprintf(src, "%d", pReq->mlt_enable);
			if ((sys_write(fd, (const char __user *) src, 2)) < 0)
			{
				printk(KERN_ERR "[Testmode MPT] Can not write MPT \n");
				goto file_fail;
			}

			fd_offset = sys_lseek(fd, 0, 0);
		}

		if ( (dest = kmalloc(5, GFP_KERNEL)) )
		{
			if ((sys_read(fd, (char __user *) dest, 2)) < 0)
			{
				printk(KERN_ERR "[Testmode MPT] Can not read MPT \n");
				goto file_fail;
			}

			if ((memcmp(src, dest, 2)) == 0)
				pRsp->ret_stat_code = TEST_OK_S;
			else
				pRsp->ret_stat_code = TEST_FAIL_S;
		}
			
        file_fail:
          kfree(src);
          kfree(dest);
          sys_close(fd);
          set_fs(old_fs);
    }

    return pRsp;
}

//                                   

//                   
PACK(void*) LGF_UTSRadio(PACK(void*) req_pkt_ptr, uint16 pkt_len)
{
	DIAG_UTS_req_type *req_ptr = (DIAG_UTS_req_type*) req_pkt_ptr;
	DIAG_UTS_RADIO_rsp_type *rsp_ptr;
	unsigned int rsp_len = sizeof(DIAG_UTS_RADIO_rsp_type);

	rsp_ptr = (DIAG_UTS_RADIO_rsp_type*) diagpkt_alloc(DIAG_UTS_RADIO, rsp_len);
	rsp_ptr->cmd_code = req_ptr->cmd_code;
	rsp_ptr->sub_cmd = req_ptr->sub_cmd;
	rsp_ptr->ret = 2;

	switch (req_ptr->data[0])
	{
	case 0:	// UTS Radio On(PowerUp)
		set_operation_mode(TRUE);	/*                                                                                                */
		rsp_ptr->ret = 0;
		break;

	case 1:	// UTS Radio Off(PowerDown)
		set_operation_mode(FALSE);	/*                                                                                                */
		rsp_ptr->ret = 1;
		break;
	}

	return rsp_ptr;
}

PACK(void*) LGF_UTSTool(PACK(void*) req_pkt_ptr, uint16 pkt_len)
{
	DIAG_UTS_req_type *req_ptr = (DIAG_UTS_req_type*) req_pkt_ptr;
	DIAG_UTS_TOOL_rsp_type *rsp_ptr;
	unsigned int rsp_len = sizeof(DIAG_UTS_TOOL_rsp_type);

	rsp_ptr = (DIAG_UTS_TOOL_rsp_type*) diagpkt_alloc(DIAG_UTS_TOOL, rsp_len);
	rsp_ptr->cmd_code = req_ptr->cmd_code;
	rsp_ptr->sub_cmd = req_ptr->sub_cmd;
	rsp_ptr->ret = 2;

	if (req_ptr->sub_cmd != 0xfd)
		return rsp_ptr;

	rsp_ptr->data_1= 1;
	rsp_ptr->ret = 0;
	return rsp_ptr;
}
//                   

int get_sw_version(char* version)
{
	DIAG_TEST_MODE_F_req_type req_ptr;
	DIAG_TEST_MODE_F_rsp_type rsp_ptr;
	memset(&rsp_ptr, 0x00, sizeof(rsp_ptr));

	// get version name
	req_ptr.sub_cmd_code = TEST_MODE_SW_VERSION;
	req_ptr.test_mode_req.sw_version = SW_VERSION;
	send_to_arm9((void*)&req_ptr, (void*)&rsp_ptr);

    if (rsp_ptr.ret_stat_code != TEST_OK_S)
		return -1;

	rsp_ptr.test_mode_rsp.model_name_buf[sizeof(rsp_ptr.test_mode_rsp.model_name_buf)-1] = '\0';
	strcpy(version, rsp_ptr.test_mode_rsp.model_name_buf);
	return 0;
}

/*  USAGE
 *  1. If you want to handle at ARM9 side, you have to insert fun_ptr as NULL and mark ARM9_PROCESSOR
 *  2. If you want to handle at ARM11 side , you have to insert fun_ptr as you want and mark AMR11_PROCESSOR.
 */

testmode_user_table_entry_type testmode_mstr_tbl[TESTMODE_MSTR_TBL_SIZE] =
{
    /* sub_command                          fun_ptr                           which procesor*/
    /* 0 ~ 10 */
    {TEST_MODE_VERSION,                     NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_LCD_CAL,                     LGF_TestLCD_Cal,                  ARM11_PROCESSOR},
    {TEST_MODE_MOTOR,                       LGF_TestMotor,                    ARM11_PROCESSOR},
    {TEST_MODE_ACOUSTIC,                    LGF_TestAcoustic,                 ARM11_PROCESSOR},
    {TEST_MODE_CAM,                         LGF_TestCam,                      ARM11_PROCESSOR},
    /* 11 ~ 20 */
    /* 21 ~ 30 */
    {TEST_MODE_KEY_TEST,                    LGT_TestModeKeyTest,              ARM11_PROCESSOR},
    {TEST_MODE_EXT_SOCKET_TEST,             LGF_ExternalSocketMemory,         ARM11_PROCESSOR},
	//                                                     
	/* Original
    {TEST_MODE_BLUETOOTH_TEST,              not_supported_command_handler,    ARM11_PROCESSOR},
	*/
	{TEST_MODE_BLUETOOTH_TEST,				LGF_TestModeBlueTooth,	          ARM11_PROCESSOR},
	// *e LG_BTUI_DIAGCMD_DUTMODE
    {TEST_MODE_BATT_LEVEL_TEST,             NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_MP3_TEST,                    LGF_TestModeMP3,                  ARM11_PROCESSOR},
    /* 31 ~ 40 */
/*                                                                    */
    {TEST_MODE_ORIENTATION_SENSOR,           linux_app_handler,               ARM11_PROCESSOR},
/*                                                                    */
    {TEST_MODE_WIFI_TEST,                   LGF_TestModeWLAN,                 ARM11_PROCESSOR},
    {TEST_MODE_MANUAL_TEST_MODE,            NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_FORMAT_MEMORY_TEST,          not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_KEY_DATA_TEST,               linux_app_handler,                ARM11_PROCESSOR},
    /* 41 ~ 50 */
    {TEST_MODE_MEMORY_CAPA_TEST,            LGF_MemoryVolumeCheck,            ARM11_PROCESSOR},
    {TEST_MODE_SLEEP_MODE_TEST,             LGF_TestModeSleepMode,            ARM11_PROCESSOR},
    {TEST_MODE_SPEAKER_PHONE_TEST,          LGF_TestModeSpeakerPhone,         ARM11_PROCESSOR},
    {TEST_MODE_VIRTUAL_SIM_TEST,            LGF_TestModeVirtualSimTest,       ARM11_PROCESSOR},
    {TEST_MODE_PHOTO_SENSER_TEST,           not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_MRD_USB_TEST,                NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_PROXIMITY_SENSOR_TEST,       linux_app_handler,                ARM11_PROCESSOR},
    {TEST_MODE_TEST_SCRIPT_MODE,            LGF_TestScriptItemSet,            ARM11_PROCESSOR},
    {TEST_MODE_FACTORY_RESET_CHECK_TEST,    LGF_TestModeFactoryReset,         ARM11_PROCESSOR},
    /* 51 ~60 */
    {TEST_MODE_VOLUME_TEST,                 LGT_TestModeVolumeLevel,          ARM11_PROCESSOR},
    {TEST_MODE_FIRST_BOOT_COMPLETE_TEST,    LGF_TestModeFBoot,                ARM11_PROCESSOR},
    {TEST_MODE_MAX_CURRENT_CHECK,           NULL,                             ARM9_PROCESSOR},
    /* 61 ~70 */
    {TEST_MODE_CHANGE_RFCALMODE,            NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_SELECT_MIMO_ANT,             NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_LTE_MODE_SELECTION,          not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_LTE_CALL,                    not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_CHANGE_USB_DRIVER,           not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_GET_HKADC_VALUE,             NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_LED_TEST,                    linux_app_handler,                ARM11_PROCESSOR},
    {TEST_MODE_PID_TEST,                    NULL,                             ARM9_PROCESSOR},
    /* 71 ~ 80 */
    {TEST_MODE_SW_VERSION,                  NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_IME_TEST,                    NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_IMPL_TEST,                   NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_SIM_LOCK_TYPE_TEST,          NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_UNLOCK_CODE_TEST,            NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_IDDE_TEST,                   NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_FULL_SIGNATURE_TEST,         NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_NT_CODE_TEST,                NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_SIM_ID_TEST,                 NULL,                             ARM9_PROCESSOR},
    /* 81 ~ 90*/
    {TEST_MODE_CAL_CHECK,                   NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_BLUETOOTH_RW,                NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_SKIP_WELCOM_TEST,            not_supported_command_handler,    ARM11_PROCESSOR},
    {TEST_MODE_WIFI_MAC_RW,                 LGF_TestModeWiFiMACRW,            ARM11_PROCESSOR},
    /* 91 ~ */
    {TEST_MODE_DB_INTEGRITY_CHECK,          LGF_TestModeDBIntegrityCheck,     ARM11_PROCESSOR},
    {TEST_MODE_NVCRC_CHECK,                 NULL,                             ARM9_PROCESSOR},
    {TEST_MODE_RESET_PRODUCTION,            NULL,                             ARM9_PROCESSOR},

//                                                 
    {TEST_MODE_FOTA_ID_CHECK,               LGF_TestModeFotaIDCheck,          ARM11_PROCESSOR},
//                                                 
	{TEST_MODE_KEY_LOCK_UNLOCK,               LGF_TestModeKeyLockUnlock,          ARM11_PROCESSOR},
//                                   
    {TEST_MODE_MLT_ENABLE,                  LGF_TestModeMLTEnableSet,         ARM11_PROCESSOR},
//                                   
/*                                                                    */
    {TEST_MODE_SENSOR_CALIBRATION_TEST,     linux_app_handler,             ARM11_PROCESSOR},
    {TEST_MODE_ACCEL_SENSOR_TEST,           linux_app_handler,                ARM11_PROCESSOR},
    {TEST_MODE_COMPASS_SENSOR_TEST,         linux_app_handler,    ARM11_PROCESSOR},
    {TEST_MODE_LCD,                         LGF_TestLCD,                      ARM11_PROCESSOR},
    {TEST_MODE_PROXIMITY_MFT_SENSOR_TEST,   linux_app_handler,         ARM11_PROCESSOR},
/*                                                                    */
    {TEST_MODE_XO_CAL_DATA_COPY,            NULL,                             ARM9_PROCESSOR},

//                                                                    
    {TESTMODE_DLOAD_MEID,                   NULL,                             ARM9_PROCESSOR},
    {TESTMODE_DLOAD_MODEL_NAME,             NULL,                             ARM9_PROCESSOR}
//                                                                    
};
