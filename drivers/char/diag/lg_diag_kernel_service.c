#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/diagchar.h>
#include <linux/sched.h>
#include <mach/usbdiag.h>
#include <asm/current.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <../../../lge/include/lg_diagcmd.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include "diagchar_hdlc.h"
#include "diagfwd.h"
#include "diagmem.h"
#include "diagchar.h"
#include "lg_diag_kernel_service.h"
#include <../../../lge/include/lg_diag_testmode.h>
#include <../../../lge/include/lg_diag_unifiedmsgtool.h> //#ifdef LG_SMS_PC_TEST
#include <../../../lge/include/lg_diagcmd.h>
#include <linux/slab.h>
#include <../../../lge/include/lg_diag_udm.h>
#include <../../../lge/include/lge_diag_eri.h>

// LG_FW : 2011.07.07 moon.yongho : saving webdload status variable to eMMC. -----------[[
#include <../../../lge/include/lg_diag_dload.h>
// LG_FW : 2011.07.07 moon.yongho -----------------------------------------------------]]

#ifdef CONFIG_LGE_DIAG_WIFI
PACK (void *)LGF_WIFI (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAGTEST
PACK (void *)LGF_TestMode (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAG_LCDQTEST
PACK (void *)LGF_LcdQTest (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAG_KEYPRESS
PACK (void *)LGF_KeyPress (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAG_SCREENSHOT
PACK (void *)LGF_ScreenShot (PACK (void *)req_pkt_ptr, uint16 pkt_len ); 
PACK (void *)LGE_ScreenSectionShot (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAG_WMC
PACK (void *)LGF_WMC (PACK (void *)req_pkt_ptr, uint16 pkt_len );
#endif

#ifdef CONFIG_LGE_DIAG_UDM
PACK (void *)LGF_Udm (PACK (void *)req_pkt_ptr, uint16 pkt_len ); 
#endif

#ifdef CONFIG_LGE_DIAG_ICD
PACK (void *)LGE_ICDProcess (PACK (void *)req_pkt_ptr, uint16 pkg_len);
#endif

//#ifdef LG_SMS_PC_TEST
PACK (void *)LGF_MsgTest (PACK (void *)req_pkt_ptr, uint16 pkg_len);
//#endif

//                           
PACK (void *)LGE_ERI (PACK (void *)req_pkt_ptr,uint16	pkt_len );
//#endif 


// LG_FW : 2011.07.07 moon.yongho : saving webdload status variable to eMMC. ----------[[
#ifdef LG_FW_WEB_DOWNLOAD	
PACK (void *)LGE_WebDload_SRD (PACK (void *)req_pkt_ptr, uint16 pkg_len);	
#endif /*LG_FW_WEB_DOWNLOAD*/	
// LG_FW : 2011.07.07 moon.yongho -----------------------------------------------------]]

//                   
PACK (void *)LGF_UTSRadio (PACK (void *)req_pkt_ptr, uint16 pkt_len);
PACK (void *)LGF_UTSTool (PACK (void *)req_pkt_ptr, uint16 pkt_len);
//                   

void diagpkt_commit (PACK(void *)pkt);

static const diagpkt_user_table_entry_type registration_table[] =
{ 
	/*{subsys cmd low, subsys cmd code high, call back function}*/
#ifdef CONFIG_LGE_DIAGTEST
	{DIAG_TEST_MODE_F, DIAG_TEST_MODE_F, LGF_TestMode},
#endif
#ifdef CONFIG_LGE_DIAG_LCDQTEST
	{DIAG_LCD_Q_TEST_F, DIAG_LCD_Q_TEST_F, LGF_LcdQTest},
#endif
#ifdef CONFIG_LGE_DIAG_KEYPRESS
	{DIAG_HS_KEY_F,DIAG_HS_KEY_F,LGF_KeyPress},
#endif
#ifdef CONFIG_LGE_DIAG_SCREENSHOT
	{DIAG_LGF_SCREEN_SHOT_F , DIAG_LGF_SCREEN_SHOT_F , LGF_ScreenShot},
	//                                                                                        
#endif
#ifdef CONFIG_LGE_DIAG_WIFI
	{DIAG_WIFI_MAC_ADDR, DIAG_WIFI_MAC_ADDR, LGF_WIFI},
#endif
#ifdef CONFIG_LGE_DIAG_WMC
	{DIAG_WMCSYNC_MAPPING_F, DIAG_WMCSYNC_MAPPING_F, LGF_WMC},
#endif
#ifdef CONFIG_LGE_DIAG_UDM
	{DIAG_UDM_SMS_MODE , DIAG_UDM_SMS_MODE , LGF_Udm},
#endif
//#ifdef LG_SMS_PC_TEST
	{DIAG_SMS_TEST_F , DIAG_SMS_TEST_F , LGF_MsgTest},
//#endif
#ifdef CONFIG_LGE_DIAG_ICD
	{DIAG_ICD_F, DIAG_ICD_F, LGE_ICDProcess},
#endif

//                   
	{DIAG_UTS_RADIO, DIAG_UTS_RADIO, LGF_UTSRadio},
	{DIAG_UTS_TOOL, DIAG_UTS_TOOL, LGF_UTSTool},
//                   

//                           
	{DIAG_ERI_CMD_F, DIAG_ERI_CMD_F, LGE_ERI},
//#endif 
// LG_FW : 2011.07.07 moon.yongho : saving webdload status variable to eMMC. ----------[[
#ifdef LG_FW_WEB_DOWNLOAD	
	{DIAG_WEBDLOAD_COMMON_F  , DIAG_WEBDLOAD_COMMON_F  , LGE_WebDload_SRD},
#endif /*LG_FW_WEB_DOWNLOAD*/	
// LG_FW : 2011.07.07 moon.yongho -----------------------------------------------------]]
};

/* This is the user dispatch table. */
static diagpkt_user_table_type *lg_diagpkt_user_table[DIAGPKT_USER_TBL_SIZE];

extern struct diagchar_dev *driver;
unsigned char read_buffer[READ_BUF_SIZE]; 
struct task_struct *lg_diag_thread;
static int num_bytes_read;

extern struct timer_list drain_timer;
extern int timer_in_progress;
extern spinlock_t diagchar_write_lock;
static 	void *buf_hdlc;
static unsigned int gPkt_commit_fail = 0;

void* lg_diag_req_pkt_ptr;

#ifdef CONFIG_LGE_DIAG_UDM
/*                                                              */
udm_sms_status_new lg_diag_req_udm_sms_status_new = {{0}};
/*                                                              */
#endif 

uint16 lg_diag_req_pkt_length;
uint16 lg_diag_rsp_pkt_length;
char lg_diag_cmd_line[LG_DIAG_CMD_LINE_LEN];

#ifdef CONFIG_LGE_DIAG_WMC
extern void* lg_diag_wmc_req_pkt_ptr;
extern uint16 lg_diag_wmc_req_pkt_length;
extern uint16 lg_diag_wmc_rsp_pkt_length;
#endif /*                   */

/* jihye.ahn  2010-11-13  for capturing video preview */
int blt_mode_enable(void);
int blt_mode_disable(void);

/*===========================================================================
  ===========================================================================*/
PACK(void *) diagpkt_alloc (diagpkt_cmd_code_type code, unsigned int length)
{
	diagpkt_lsm_rsp_type *item = NULL;
	diagpkt_hdr_type *pkt = NULL;
	PACK(uint16 *)pattern = NULL;    /* Overrun pattern. */
	unsigned char *p;
	diag_data* pdiag_data = NULL;
	unsigned int size = 0;

	size = DIAG_REST_OF_DATA_POS + FPOS (diagpkt_lsm_rsp_type, rsp.pkt) + length + sizeof (uint16);
   
	/*-----------------------------------------------
	  Try to allocate a buffer.  Size of buffer must
	  include space for overhead and CRC at the end.
	  -----------------------------------------------*/
	// allocate memory and set initially 0.
	//pdiag_data = (diag_data*)kmalloc (size, GFP_KERNEL);
	pdiag_data = (diag_data*)kmalloc (size, GFP_KERNEL);
	if(NULL == pdiag_data)
	{
		/* Alloc not successful.  Return NULL. DiagSvc_Malloc() allocates memory
		   from client's heap using a malloc call if the pre-malloced buffers are not available.
		   So if this fails, it means that the client is out of heap. */
		return NULL;
	}
	/* Fill in the fact that this is a response */
	pdiag_data->diag_data_type = DIAG_DATA_TYPE_RESPONSE;
	// WM7 prototyping: advance the pointer now
	item = (diagpkt_lsm_rsp_type*)((byte*)(pdiag_data)+DIAG_REST_OF_DATA_POS);
	
	/* This pattern is written to verify pointers elsewhere in this
	   service  are valid. */
	item->rsp.pattern = DIAGPKT_HDR_PATTERN;    /* Sanity check pattern */
    
	/* length ==  size unless packet is resized later */
	item->rsp.size = length;
	item->rsp.length = length;

	pattern = (PACK(uint16 *)) & item->rsp.pkt[length];

	/* We need this to meet alignment requirements - MATS */
	p = (unsigned char *) pattern;
	p[0] = (DIAGPKT_OVERRUN_PATTERN >> 8) & 0xff;
	p[1] = (DIAGPKT_OVERRUN_PATTERN >> 0) & 0xff;

	pkt = (diagpkt_hdr_type *) & item->rsp.pkt;

	if (pkt)
	{
		pkt->command_code = code;
	}
	return (PACK(void *)) pkt;
}               /* diagpkt_alloc */
EXPORT_SYMBOL(diagpkt_alloc);

void diagpkt_free(PACK(void *)pkt)
{
	if (pkt)
	{
		byte *item = (byte*)DIAGPKT_PKT2LSMITEM(pkt);
		item -= DIAG_REST_OF_DATA_POS;
		kfree ((void *)item);
	}
	return;
}
EXPORT_SYMBOL(diagpkt_free);

static ssize_t read_cmd_pkt(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	memcpy(buf, lg_diag_req_pkt_ptr, lg_diag_req_pkt_length);
  
	return lg_diag_req_pkt_length;
}

static ssize_t write_cmd_pkt(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	void* rsp_pkt_ptr;
#ifdef LG_DIAG_DEBUG
	int i;
#endif

	printk(KERN_ERR "\n LG_FW : print received packet :len(%d) \n",lg_diag_rsp_pkt_length);
	rsp_pkt_ptr = (DIAG_TEST_MODE_F_rsp_type *)diagpkt_alloc(DIAG_TEST_MODE_F, lg_diag_rsp_pkt_length);
	memcpy(rsp_pkt_ptr, buf, lg_diag_rsp_pkt_length);

#ifdef LG_DIAG_DEBUG
	for (i=0;i<lg_diag_rsp_pkt_length;i++) {
		printk(KERN_ERR "0x%x ",*((unsigned char*)(rsp_pkt_ptr + i)));
	}
	printk(KERN_ERR "\n");
#endif
	diagpkt_commit(rsp_pkt_ptr);
	return size;
}

static ssize_t read_cmd_pkt_length(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	int read_len = 2;
  
	memcpy(buf, &lg_diag_req_pkt_length, read_len);
	return read_len;
}

static ssize_t write_cmd_pkt_length(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int write_len = 2;

	memcpy((void*)&lg_diag_rsp_pkt_length, buf, write_len);
	printk( KERN_DEBUG "LG_FW : write_cmd_pkt_length = %d\n",lg_diag_rsp_pkt_length);  
	return write_len;
}


#ifdef CONFIG_LGE_DIAG_WMC
static ssize_t read_wmc_cmd_pkt(struct device *dev, struct device_attribute *attr,
				char *buf)
{

	printk(KERN_INFO "%s, attr_name : %s, length : %d\n", __func__, attr->attr.name, lg_diag_wmc_req_pkt_length);

	// handle only if the buffer is not empty, or return error.
	if(buf != NULL)
	{
		memcpy(buf, lg_diag_wmc_req_pkt_ptr, lg_diag_wmc_req_pkt_length);
		return lg_diag_wmc_req_pkt_length;
	}
  
	printk(KERN_ERR "%s, NULL buf, return error\n", __func__);
	return -EINVAL;
}

static ssize_t write_wmc_cmd_pkt(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	void* rsp_pkt_ptr = NULL;
#ifdef LG_DIAG_DEBUG
	int i;
#endif

	printk(KERN_INFO "%s : print received packet :len(%d) \n",__func__, lg_diag_wmc_rsp_pkt_length);
	rsp_pkt_ptr = (void *)diagpkt_alloc(DIAG_WMCSYNC_MAPPING_F, lg_diag_wmc_rsp_pkt_length);

	// handle only if allocation is not empty, or return busy.
	if(rsp_pkt_ptr != NULL && buf != NULL)
	{
		memcpy(rsp_pkt_ptr, buf, lg_diag_wmc_rsp_pkt_length);

#ifdef LG_DIAG_DEBUG
		for (i=0;i<lg_diag_wmc_rsp_pkt_length;i++) {
			printk(KERN_INFO "0x%x ",*((unsigned char*)(rsp_pkt_ptr + i)));
		}
		printk(KERN_INFO "\n");
#endif
		diagpkt_commit(rsp_pkt_ptr);
		return size;
	}

	printk(KERN_ERR "%s, allocation failed, return error\n", __func__);
	return -EBUSY;
}

static ssize_t read_wmc_cmd_pkt_length(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	int read_len = 2;

	printk(KERN_INFO "%s, attr_name : %s, length : %d\n", __func__, attr->attr.name, lg_diag_wmc_req_pkt_length);

	// handle only if the buffer is not empty, or return error.
	if(buf != NULL)
	{
		memcpy(buf, &lg_diag_wmc_req_pkt_length, read_len);
		return read_len;
	}

	printk(KERN_ERR "%s, NULL buf, return error\n", __func__);
	return -EINVAL;
}

static ssize_t write_wmc_cmd_pkt_length(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{

	int write_len = 2;

	printk(KERN_INFO "%s, attr_name : %s\n", __func__, attr->attr.name);

	if(buf != NULL && &lg_diag_wmc_rsp_pkt_length != NULL ){
		memcpy((void*)&lg_diag_wmc_rsp_pkt_length, buf, write_len);
		printk( KERN_INFO "LG_FW : write_cmd_pkt_length = %d\n",lg_diag_wmc_rsp_pkt_length);  
		return write_len;
	}
	printk(KERN_ERR "%s : received packet : %d, actual : %d\n",__func__, sizeof(buf), write_len);
	return -EINVAL;
  
}
#endif /*                   */

#ifdef CONFIG_LGE_DIAG_UDM
/*                                                              */
static ssize_t read_sms_status_new(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	int udm_sms_statu_len = sizeof(udm_sms_status_new);
  
	memcpy(buf, &lg_diag_req_udm_sms_status_new, udm_sms_statu_len);
	return udm_sms_statu_len;
}

static ssize_t write_sms_status_new(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int udm_sms_statu_len = sizeof(udm_sms_status_new);

	memcpy((void*)&lg_diag_req_udm_sms_status_new, buf, udm_sms_statu_len);
	//  printk( KERN_DEBUG "LG_FW : write_cmd_pkt_length = %d\n",lg_diag_rsp_pkt_length);  
	return udm_sms_statu_len;
}
#endif 
/*                                                              */

/*                                                                                           */
static DEVICE_ATTR(cmd_pkt, S_IRUGO | S_IWUSR,read_cmd_pkt, write_cmd_pkt);
static DEVICE_ATTR(length, S_IRUGO | S_IWUSR,read_cmd_pkt_length, write_cmd_pkt_length);

#ifdef CONFIG_LGE_DIAG_WMC
static DEVICE_ATTR(wmc_cmd_pkt, S_IRUGO | S_IWUSR,read_wmc_cmd_pkt, write_wmc_cmd_pkt);
static DEVICE_ATTR(wmc_length, S_IRUGO | S_IWUSR,read_wmc_cmd_pkt_length, write_wmc_cmd_pkt_length);
#endif /*                   */

/*                                                              */
#ifdef CONFIG_LGE_DIAG_UDM
static DEVICE_ATTR(get_sms, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
static DEVICE_ATTR(set_sms, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
static DEVICE_ATTR(sms_status, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
static DEVICE_ATTR(rsp_get_sms, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
static DEVICE_ATTR(rsp_set_sms, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
static DEVICE_ATTR(rsp_sms_status, S_IRUGO | S_IWUSR,read_sms_status_new, write_sms_status_new);
#endif 
/*                                                              */

int lg_diag_create_file(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_cmd_pkt);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_cmd_pkt);
		return ret;
	}
	
	ret = device_create_file(&pdev->dev, &dev_attr_length);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file2 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_length);
		return ret;
	}
	/*                                                                                           */
#if 0    //                                                                                                             
	ret = device_create_file(&pdev->dev, &dev_attr_wlan_status);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_wlan_status);
		return ret;
	}
#endif
	/*                                                                                           */	
#ifdef CONFIG_LGE_DIAG_WMC
	ret = device_create_file(&pdev->dev, &dev_attr_wmc_cmd_pkt);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file4 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_wmc_cmd_pkt);
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_wmc_length);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file5 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_wmc_length);
		return ret;
	}
#endif /*                   */

	/*                                                              */
#ifdef CONFIG_LGE_DIAG_UDM
        ret = device_create_file(&pdev->dev, &dev_attr_sms_status);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_sms_status);
		return ret;
	}
  
	ret = device_create_file(&pdev->dev, &dev_attr_get_sms);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_get_sms);
		return ret;
	}
  
	ret = device_create_file(&pdev->dev, &dev_attr_set_sms);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_set_sms);
		return ret;
	}
  
	ret = device_create_file(&pdev->dev, &dev_attr_rsp_sms_status);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_rsp_sms_status);
		return ret;
	}
  
	ret = device_create_file(&pdev->dev, &dev_attr_rsp_get_sms);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_rsp_get_sms);
		return ret;
	}
  
	ret = device_create_file(&pdev->dev, &dev_attr_rsp_set_sms);
	if (ret) {
		printk( KERN_DEBUG "LG_FW : diag device file3 create fail\n");
		device_remove_file(&pdev->dev, &dev_attr_rsp_set_sms);
		return ret;
	}
#endif
	/*                                                              */

	return ret;
}
EXPORT_SYMBOL(lg_diag_create_file);

int lg_diag_remove_file(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_cmd_pkt);

#ifdef CONFIG_LGE_DIAG_UDM
	/*                                                              */
	device_remove_file(&pdev->dev, &dev_attr_sms_status);
	device_remove_file(&pdev->dev, &dev_attr_get_sms);
	device_remove_file(&pdev->dev, &dev_attr_set_sms);
	device_remove_file(&pdev->dev, &dev_attr_rsp_sms_status);
	device_remove_file(&pdev->dev, &dev_attr_rsp_get_sms);
	device_remove_file(&pdev->dev, &dev_attr_rsp_set_sms);
	/*                                                              */
#endif
	device_remove_file(&pdev->dev, &dev_attr_length);

#ifdef CONFIG_LGE_DIAG_WMC
	device_remove_file(&pdev->dev, &dev_attr_wmc_cmd_pkt);
	device_remove_file(&pdev->dev, &dev_attr_wmc_length);
#endif /*                   */
	return 0;
}
EXPORT_SYMBOL(lg_diag_remove_file);

static int lg_diag_app_execute(void)
{
	int ret;
	char cmdstr[100];
	int fd;
	char *envp[] = {
		"HOME=/",
		"TERM=linux",
		NULL,
	};

	char *argv[] = {
		"sh",
		"-c",
		cmdstr,
		NULL,
	};	

	//                                      
	// 0001794: [ARM9] ATS AT CMD added 
	if ( (fd = sys_open((const char __user *) "/system/bin/lg_diag_app", O_RDONLY ,0) ) < 0 )
	{
		printk("\n can not open /system/bin/lg_diag - execute /system/bin/lg_diag_app\n");
		sprintf(cmdstr, "/system/bin/lg_diag_app\n");
	}
	else
	{
		printk("\n execute /system/bin/lg_diag_app\n");
		sprintf(cmdstr, "/system/bin/lg_diag_app\n");
		sys_close(fd);
	}
	//                                    

	printk(KERN_INFO "execute - %s", cmdstr);
	if ((ret = call_usermodehelper("/system/bin/sh", argv, envp, UMH_WAIT_PROC)) != 0) {
		printk(KERN_ERR "LG_DIAG failed to run \": %i\n",
		       ret);
	}
	else
		printk(KERN_INFO "LG_DIAG execute ok");
	return ret;
}


/*                                                         */
int blt_mode_enable(void)
{
	int ret;
	char cmdstr[100];
	int fd;
	char *envp[] = {
		"HOME=/",
		"TERM=linux",
		NULL,
	};

	char *argv[] = {
		"sh",
		"-c",
		cmdstr,
		NULL,	
	};	

	//                                      
	// 0001794: [ARM9] ATS AT CMD added 
	if ( (fd = sys_open((const char __user *) "/system/bin/enablebltmode", O_RDONLY ,0) ) < 0 )
	{
		//printk("\n can not open /system/bin/lg_diag - execute /system/bin/lg_diag_app\n");
		sprintf(cmdstr, "/system/bin/enablebltmode\n");
	}
	else
	{
		printk("\n execute /system/bin/enablebltmode\n");
		sprintf(cmdstr, "/system/bin/enablebltmode\n");
		sys_close(fd);
	}
	//                                    

	printk(KERN_INFO "execute - %s", cmdstr);
	if ((ret = call_usermodehelper("/system/bin/sh", argv, envp, UMH_WAIT_PROC)) != 0) {
		printk(KERN_ERR "jihye.ahn          LG_DIAG failed to run \": %i\n",
		       ret);
	}
	else
		printk(KERN_INFO "jihye.ahn          LG_DIAG execute ok");
	return ret;
}
EXPORT_SYMBOL(blt_mode_enable);

int blt_mode_disable(void)
{
	int ret;
	char cmdstr[100];
	int fd;
	char *envp[] = {
		"HOME=/",
		"TERM=linux",
		NULL,
	};

	char *argv[] = {
		"sh",
		"-c",
		cmdstr,
		NULL,	
	};	

	//                                      
	// 0001794: [ARM9] ATS AT CMD added 
	if ( (fd = sys_open((const char __user *) "/system/bin/disablebltmode", O_RDONLY ,0) ) < 0 )
	{
		//printk("\n can not open /system/bin/lg_diag - execute /system/bin/lg_diag_app\n");
		sprintf(cmdstr, "/system/bin/disablebltmode\n");
	}
	else
	{
		printk("\n execute /system/bin/disablebltmode\n");
		sprintf(cmdstr, "/system/bin/disablebltmode\n");
		sys_close(fd);
	}
	//                                    

	printk(KERN_INFO "execute - %s", cmdstr);
	if ((ret = call_usermodehelper("/system/bin/sh", argv, envp, UMH_WAIT_PROC)) != 0) {
		printk(KERN_ERR "jihye.ahn          LG_DIAG failed to run \": %i\n",
		       ret);
	}
	else
		printk(KERN_INFO "jihye.ahn          LG_DIAG execute ok");
	return ret;
}
EXPORT_SYMBOL(blt_mode_disable);
/*                                                         */
static int diagchar_open(void)
{
	int i = 0;

	if (driver) {
		mutex_lock(&driver->diagchar_mutex);

		for (i = 0; i < driver->num_clients; i++)
			//                                              
			// MOD 0009214: [DIAG] LG Diag feature added in side of android
			if (driver->client_map[i].pid == 0)
				//                                            
				break;

		if (i < driver->num_clients)
		{
			//                                              
			// MOD 0009214: [DIAG] LG Diag feature added in side of android
			driver->client_map[i].pid = current->tgid;
			//                                            
#ifdef LG_DIAG_DEBUG
			printk(KERN_DEBUG "LG_FW : client_map id = 0x%x\n", driver->client_map[i].pid);
#endif
		}
		else
		{
			mutex_unlock(&driver->diagchar_mutex);
			return -ENOMEM;
		}

		driver->data_ready[i] |= MSG_MASKS_TYPE;
		driver->data_ready[i] |= EVENT_MASKS_TYPE;
		driver->data_ready[i] |= LOG_MASKS_TYPE;

		if (driver->ref_count == 0)
			diagmem_init(driver);
		driver->ref_count++;
		mutex_unlock(&driver->diagchar_mutex);
		return 0;
	}
	return -ENOMEM;
}

static int diagchar_ioctl_servie(unsigned int iocmd, unsigned long ioarg)
{
	int i, count_entries = 0;
	int success = -1;

	if (iocmd == DIAG_IOCTL_COMMAND_REG) {
		struct bindpkt_params_per_process *pkt_params =
			(struct bindpkt_params_per_process *) ioarg;

		for (i = 0; i < diag_max_registration; i++) { // REG_TABLE_SIZE -> diag_max_registration
			if (driver->table[i].process_id == 0) {
				driver->table[i].cmd_code =
					pkt_params->params->cmd_code;
				driver->table[i].subsys_id =
					pkt_params->params->subsys_id;
				driver->table[i].cmd_code_lo =
					pkt_params->params->cmd_code_hi;
				driver->table[i].cmd_code_hi =
					pkt_params->params->cmd_code_lo;
				driver->table[i].process_id = current->tgid;
				count_entries++;
				success = 0;
				if (pkt_params->count > count_entries)
					pkt_params->params++;
				else
					break;
			}
		}
	}
	return success;
}

static int diagchar_read(char *buf, int count )
{
	int index = -1, i = 0, ret = 0;
	int data_type;
#ifdef LG_DIAG_DEBUG
	printk(KERN_DEBUG "LG_FW : %s enter \n",__func__);
#endif
	for (i = 0; i < driver->num_clients; i++)
		//                                              
		// MOD 0009214: [DIAG] LG Diag feature added in side of android
		if (driver->client_map[i].pid == current->tgid)
			//                                            
			index = i;

	if (index == -1)
	{
#ifdef LG_DIAG_DEBUG
		printk(KERN_DEBUG "LG_FW : client_map id not found \n");
#endif
		return -EINVAL;
	}
	wait_event_interruptible(driver->wait_q,
				 driver->data_ready[index]);

#ifdef LG_DIAG_DEBUG
	printk(KERN_DEBUG "LG_FW : diagchar_read	data_ready\n");
#endif

	mutex_lock(&driver->diagchar_mutex);

#ifdef LG_DIAG_DEBUG
	printk("\nLG_FW : driver->data_ready:\n");
	for(i=0;i<20;i++)
	{
		printk("[0x%x] ",driver->data_ready[i]);
	}
	printk("\n\n");
#endif
	if (driver->data_ready[index] & DEINIT_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & DEINIT_TYPE;
		memcpy(buf, (void *)&data_type, 4);

		ret += 4;
		driver->data_ready[index] ^= DEINIT_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & MSG_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & MSG_MASKS_TYPE;
		memcpy(buf, (void *)&data_type, 4);
		ret += 4;

		memcpy(buf+4, (void *)driver->msg_masks, MSG_MASK_SIZE);
		ret += MSG_MASK_SIZE;
		driver->data_ready[index] ^= MSG_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & EVENT_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & EVENT_MASKS_TYPE;
		memcpy(buf, (void *)&data_type, 4);
		ret += 4;
		memcpy(buf+4, (void *)driver->event_masks, EVENT_MASK_SIZE);
		ret += EVENT_MASK_SIZE;
		driver->data_ready[index] ^= EVENT_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & LOG_MASKS_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & LOG_MASKS_TYPE;
		memcpy(buf, (void *)&data_type, 4);
		ret += 4;

		memcpy(buf+4, (void *)driver->log_masks,LOG_MASK_SIZE);
		ret += LOG_MASK_SIZE;
		driver->data_ready[index] ^= LOG_MASKS_TYPE;
		goto exit;
	}

	if (driver->data_ready[index] & PKT_TYPE) {
		/*Copy the type of data being passed*/
		data_type = driver->data_ready[index] & PKT_TYPE;
		memcpy(buf, (void *)&data_type, 4);
		ret += 4;

		memcpy(buf+4, (void *)driver->pkt_buf,
		       driver->pkt_length);
		ret += driver->pkt_length;
		driver->data_ready[index] ^= PKT_TYPE;
		goto exit;
	}

 exit:
	mutex_unlock(&driver->diagchar_mutex);
	return ret;
}

static int diagchar_write( const char *buf, size_t count)
{
	int  err, ret = 0, pkt_type;
#ifdef LG_DIAG_DEBUG
	int length = 0, i;
#endif
	struct diag_send_desc_type send = { NULL, NULL, DIAG_STATE_START, 0 };
	struct diag_hdlc_dest_type enc = { NULL, NULL, 0 };
	void *buf_copy = NULL;
	int payload_size;

	if (!timer_in_progress)	{
		timer_in_progress = 1;
		ret = mod_timer(&drain_timer, jiffies + msecs_to_jiffies(500));
	}
	if (!driver->usb_connected) {
		/*Drop the diag payload */
		return -EIO;
	}

	/* Get the packet type F3/log/event/Pkt response */
	memcpy((&pkt_type), buf, 4);
	/*First 4 bytes indicate the type of payload - ignore these */
	payload_size = count - 4;

	buf_copy = diagmem_alloc(driver, payload_size, POOL_TYPE_COPY);
	if (!buf_copy) {
		driver->dropped_count++;
		return -ENOMEM;
	}

	memcpy(buf_copy, buf + 4, payload_size);
#ifdef LG_DIAG_DEBUG
	printk(KERN_DEBUG "LG_FW : data is --> \n");
	for (i = 0; i < payload_size; i++)
		printk(KERN_DEBUG "\t %x \t", *(((unsigned char *)buf_copy)+i));
#endif
	send.state = DIAG_STATE_START;
	send.pkt = buf_copy;
	send.last = (void *)(buf_copy + payload_size - 1);
	send.terminate = 1;
#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "\n LG_FW : 1 Already used bytes in buffer %d, and"
	       " incoming payload size is %d \n", driver->used, payload_size);
#endif
	mutex_lock(&driver->diagchar_mutex);
	if (!buf_hdlc)
		buf_hdlc = diagmem_alloc(driver, HDLC_OUT_BUF_SIZE,
					 POOL_TYPE_HDLC);

	if (HDLC_OUT_BUF_SIZE - driver->used <= payload_size + 7) {
		driver->write_ptr_svc = (struct diag_request *)
			(diagmem_alloc(driver, sizeof(struct diag_request),
				       POOL_TYPE_WRITE_STRUCT));

		if (!driver->write_ptr_svc)
			return 0;
		
		/*           
                                           
                                    
   */
		if(!driver->write_ptr_svc) {
			diagmem_free(driver,buf_hdlc,POOL_TYPE_HDLC);
			ret = -ENOMEM;
			goto fail_free_hdlc;
		}
		driver->write_ptr_svc->buf = buf_hdlc;
		driver->write_ptr_svc->length = driver->used;
		err = usb_diag_write(driver->legacy_ch,
				     driver->write_ptr_svc);
		if (err) {
			diagmem_free(driver, driver->write_ptr_svc, POOL_TYPE_WRITE_STRUCT);
			/*Free the buffer right away if write failed */
			diagmem_free(driver, buf_hdlc, POOL_TYPE_HDLC);
			ret = -EIO;
			goto fail_free_hdlc;
		}
		buf_hdlc = NULL;
#ifdef LG_DIAG_DEBUG
		printk(KERN_INFO "\nLG_FW : size written is %d \n", driver->used);
#endif
		driver->used = 0;
		buf_hdlc = diagmem_alloc(driver, HDLC_OUT_BUF_SIZE,
					 POOL_TYPE_HDLC);
		if (!buf_hdlc) {
			ret = -ENOMEM;
			goto fail_free_hdlc;
		}
	}

	enc.dest = buf_hdlc + driver->used;
	// LG_FW khlee 2010.02.01 - to support screen capture, In that case, it has too many 'ESC_CHAR'
	/*                                                                        */
	enc.dest_last = (void *)(buf_hdlc + HDLC_OUT_BUF_SIZE -1);

	/* LG_CHANGES_E -#else*/
	/*               
                                                                        
         
 */
	diag_hdlc_encode(&send, &enc);

#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "\n LG_FW : 2 Already used bytes in buffer %d, and"
	       " incoming payload size is %d \n", driver->used, payload_size);
	printk(KERN_DEBUG "LG_FW : hdlc encoded data is --> \n");
	for (i = 0; i < payload_size + 8; i++) {
		printk(KERN_DEBUG "\t %x \t", *(((unsigned char *)buf_hdlc)+i));
		if (*(((unsigned char *)buf_hdlc)+i) != 0x7e)
			length++;
	}
#endif

	/* This is to check if after HDLC encoding, we are still within the
	   limits of aggregation buffer. If not, we write out the current buffer
	   and start aggregation in a newly allocated buffer */
	if ((unsigned int) enc.dest >=
	    (unsigned int)(buf_hdlc + HDLC_OUT_BUF_SIZE)) {
		driver->write_ptr_svc = (struct diag_request *)
			(diagmem_alloc(driver, sizeof(struct diag_request),
				       POOL_TYPE_WRITE_STRUCT));
		driver->write_ptr_svc->buf = buf_hdlc;
		driver->write_ptr_svc->length = driver->used;
		err = usb_diag_write(driver->legacy_ch,
				     driver->write_ptr_svc);
		if (err) {
			diagmem_free(driver, driver->write_ptr_svc, POOL_TYPE_WRITE_STRUCT);
			/*Free the buffer right away if write failed */
			diagmem_free(driver, buf_hdlc, POOL_TYPE_HDLC);
			ret = -EIO;
			goto fail_free_hdlc;
		}
		buf_hdlc = NULL;
#ifdef LG_DIAG_DEBUG
		printk(KERN_INFO "\nLG_FW : size written is %d \n", driver->used);
#endif
		driver->used = 0;
		buf_hdlc = diagmem_alloc(driver, HDLC_OUT_BUF_SIZE,
					 POOL_TYPE_HDLC);
		if (!buf_hdlc) {
			ret = -ENOMEM;
			goto fail_free_hdlc;
		}
		enc.dest = buf_hdlc + driver->used;
		enc.dest_last = (void *)(buf_hdlc + driver->used +
					 payload_size + 7);
		diag_hdlc_encode(&send, &enc);
#ifdef LG_DIAG_DEBUG
		printk(KERN_INFO "\n LG_FW : 3 Already used bytes in buffer %d, and"
		       " incoming payload size is %d \n", driver->used, payload_size);
		printk(KERN_DEBUG "LG_FW : hdlc encoded data is --> \n");
		for (i = 0; i < payload_size + 8; i++) {
			printk(KERN_DEBUG "\t %x \t", *(((unsigned char *)buf_hdlc)+i));
			if (*(((unsigned char *)buf_hdlc)+i) != 0x7e)
				length++;
		}
#endif
	}

	driver->used = (uint32_t) enc.dest - (uint32_t) buf_hdlc;
	if (pkt_type == DATA_TYPE_RESPONSE) {
		driver->write_ptr_svc = (struct diag_request *)
			(diagmem_alloc(driver, sizeof(struct diag_request),
				       POOL_TYPE_WRITE_STRUCT));
		driver->write_ptr_svc->buf = buf_hdlc;
		driver->write_ptr_svc->length = driver->used;
		err = usb_diag_write(driver->legacy_ch,
				     driver->write_ptr_svc);
		if (err) {
			diagmem_free(driver, driver->write_ptr_svc, POOL_TYPE_WRITE_STRUCT);
			/*Free the buffer right away if write failed */
			diagmem_free(driver, buf_hdlc, POOL_TYPE_HDLC);
			ret = -EIO;
			goto fail_free_hdlc;
		}
		buf_hdlc = NULL;
#ifdef LG_DIAG_DEBUG
		printk(KERN_INFO "\nLG_FW : size written is %d \n", driver->used);
#endif
		driver->used = 0;
	}

	mutex_unlock(&driver->diagchar_mutex);
	diagmem_free(driver, buf_copy, POOL_TYPE_COPY);
	return 0;

 fail_free_hdlc:
	diagmem_free(driver, buf_copy, POOL_TYPE_COPY);
	mutex_unlock(&driver->diagchar_mutex);
	return ret;

}

static void diagpkt_user_tbl_init (void)
{
	int i = 0;
	static boolean initialized = FALSE;

#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "LG_FW : %s enter\n",__func__);
#endif

	if (!initialized)
	{
		for (i = 0; (i < DIAGPKT_USER_TBL_SIZE); i++)
		{
			lg_diagpkt_user_table[i] = NULL;
		}
		initialized = TRUE;
	}
}

void 	diagpkt_tbl_reg (const diagpkt_user_table_type * tbl_ptr)
{
	int i = 0;
	//int mem_alloc_count = 0;
	word num_entries = tbl_ptr->count;
	bindpkt_params *bind_req = (bindpkt_params*)kmalloc(sizeof(bindpkt_params) * num_entries, GFP_KERNEL);
	bindpkt_params_per_process bind_req_send;

#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "LG_FW : %s enter, bind_req = 0x%x\n",__func__,(uint32_t)bind_req);
#endif
	if(NULL != bind_req)
	{
		/* Make sure this is initialized */
		diagpkt_user_tbl_init ();
		for (i = 0; i < DIAGPKT_USER_TBL_SIZE; i++)
		{
			if (lg_diagpkt_user_table[i] == NULL)
			{
				lg_diagpkt_user_table[i] = (diagpkt_user_table_type *)
				kmalloc(sizeof(diagpkt_user_table_type), GFP_KERNEL);
				if (NULL == lg_diagpkt_user_table[i])
				{
					printk(KERN_ERR "LG_FW : diagpkt_tbl_reg: malloc failed.");
					kfree (bind_req);
					return;
				}
				memcpy(lg_diagpkt_user_table[i], tbl_ptr, sizeof(diagpkt_user_table_type));
				break;
			}
		}
		bind_req_send.count = num_entries;
		//sprintk(bind_req_send.sync_obj_name, "%s%d", DIAG_LSM_PKT_EVENT_PREFIX, gdwClientID);

		for (i = 0; i < num_entries; i++)
		{
			bind_req[i].cmd_code = tbl_ptr->cmd_code;
			bind_req[i].subsys_id = tbl_ptr->subsysid;
			bind_req[i].cmd_code_lo = tbl_ptr->user_table[i].cmd_code_lo;
			bind_req[i].cmd_code_hi = tbl_ptr->user_table[i].cmd_code_hi;
			bind_req[i].proc_id = tbl_ptr->proc_id;
			bind_req[i].event_id = 0;
			bind_req[i].log_code = 0;
			//bind_req[i].client_id = gdwClientID;
#ifdef LG_DIAG_DEBUG
			printk(KERN_ERR "\n LG_FW : params are %d \t%d \t%d \t%d \t%d \t \n", bind_req[i].cmd_code, bind_req[i].subsys_id, 
			       bind_req[i].cmd_code_lo, bind_req[i].cmd_code_hi, bind_req[i].proc_id	);
#endif
		}
		bind_req_send.params = bind_req;
		if(diagchar_ioctl_servie(DIAG_IOCTL_COMMAND_REG, (unsigned long)&bind_req_send))
		{
			printk(KERN_ERR "LG_FW :  diagpkt_tbl_reg: DeviceIOControl failed. \n");
		}
		kfree (bind_req);
	} /* if(NULL != bind_req) */	
}


void diagpkt_commit (PACK(void *)pkt)
{
#ifdef LG_DIAG_DEBUG
	int i;
#endif

	if (pkt)
	{
		unsigned int length = 0;
		unsigned char *temp = NULL;
		int type = DIAG_DATA_TYPE_RESPONSE;

		diagpkt_lsm_rsp_type *item = DIAGPKT_PKT2LSMITEM (pkt);
		item->rsp_func = NULL;
		item->rsp_func_param = NULL;
		/* end mobile-view */
#ifdef LG_DIAG_DEBUG
		printk(KERN_ERR "\n LG_FW : printing buffer at top \n");
		for(i=0;i<item->rsp.length;i++)
			printk(KERN_ERR "0x%x ", ((unsigned char*)(pkt))[i]);      
#endif
		length = DIAG_REST_OF_DATA_POS + FPOS(diagpkt_lsm_rsp_type, rsp.pkt) + item->rsp.length + sizeof(uint16);

		if (item->rsp.length > 0)
		{
			temp =  (unsigned char*) kmalloc((int)DIAG_REST_OF_DATA_POS + (int)(item->rsp.length), GFP_KERNEL);
			memcpy(temp, (unsigned char*)&type, DIAG_REST_OF_DATA_POS);
			memcpy(temp+4, pkt, item->rsp.length);
		
#ifdef LG_DIAG_DEBUG
			printk(KERN_ERR "\n LG_FW : printing buffer %d \n",(int)(item->rsp.length + DIAG_REST_OF_DATA_POS));

			for(i=0; i < (int)(item->rsp.length + DIAG_REST_OF_DATA_POS) ;i++)
				printk(KERN_ERR "0x%x ", ((unsigned char*)(temp))[i]);      
                  
			printk(KERN_ERR "\n");
#endif
			if(diagchar_write((const void*) temp, item->rsp.length + DIAG_REST_OF_DATA_POS)) /*TODO: Check the Numberofbyteswritten against number of bytes we wanted to write?*/
			{
				printk(KERN_ERR "\n LG_FW : Diag_LSM_Pkt: WriteFile Failed in diagpkt_commit \n");
				gPkt_commit_fail++;
			}
		}

		kfree(temp);
		diagpkt_free(pkt);
	} /* end if (pkt)*/
  	return;
}               /* diagpkt_commit */

diagpkt_cmd_code_type diagpkt_get_cmd_code (PACK(void *)ptr)
{
	diagpkt_cmd_code_type cmd_code = 0;
	if(ptr)
	{
		/* Diag command codes are the first byte */
		return *((diagpkt_cmd_code_type *) ptr);
	}
	return cmd_code;
} /* diag_get_cmd_code */

diagpkt_subsys_id_type diagpkt_subsys_get_id (PACK(void *)ptr)
{
	diagpkt_subsys_id_type id = 0;
	if (ptr)
	{
		diagpkt_subsys_hdr_type *pkt_ptr = (void *) ptr;

		if ((pkt_ptr->command_code == DIAG_SUBSYS_CMD_F) || 
		    (pkt_ptr->command_code == DIAG_SUBSYS_CMD_VER_2_F)) 
		{
			id = (diagpkt_subsys_id_type) pkt_ptr->subsys_id;
		} 
		else 
		{
			id = 0xFF;
		}
	}
	return id;
} /* diagpkt_subsys_get_id */

diagpkt_subsys_cmd_code_type diagpkt_subsys_get_cmd_code (PACK(void *)ptr)
{
	diagpkt_subsys_cmd_code_type code = 0;
	if(ptr)
	{
		diagpkt_subsys_hdr_type *pkt_ptr = (void *) ptr;

		if ((pkt_ptr->command_code == DIAG_SUBSYS_CMD_F) || 
		    (pkt_ptr->command_code == DIAG_SUBSYS_CMD_VER_2_F)) 
		{
			code = pkt_ptr->subsys_cmd_code;
		} 
		else 
		{
			code = 0xFFFF;
		}
	}
	return code;
} /* diagpkt_subsys_get_cmd_code */

void diagpkt_process_request (void *req_pkt, uint16 pkt_len,
			      diag_cmd_rsp rsp_func, void *rsp_func_param)
{
	uint16 packet_id;     /* Command code for std or subsystem */
	uint8 subsys_id = DIAGPKT_NO_SUBSYS_ID;
	const diagpkt_user_table_type *user_tbl_entry = NULL;
	const diagpkt_user_table_entry_type *tbl_entry = NULL;
	int tbl_entry_count = 0;
	int i,j;
	void *rsp_pkt = NULL;
	boolean found = FALSE;
	uint16 cmd_code = 0xFF;

#ifdef LG_DIAG_DEBUG
	printk(KERN_ERR "\n LG_FW : print received packet \n");
	for (i=0;i<pkt_len;i++) {
		printk(KERN_ERR "0x%x ",*((unsigned char*)(req_pkt + i)));
	}
	printk(KERN_ERR "\n");
#endif
	packet_id = diagpkt_get_cmd_code (req_pkt);

	if ( packet_id == DIAG_SUBSYS_CMD_VER_2_F )
	{
		cmd_code = packet_id;
	}
    
	if ((packet_id == DIAG_SUBSYS_CMD_F) || ( packet_id == DIAG_SUBSYS_CMD_VER_2_F ))
	{
		subsys_id = diagpkt_subsys_get_id (req_pkt);
		packet_id = diagpkt_subsys_get_cmd_code (req_pkt);
	}

	/* Search the dispatch table for a matching subsystem ID.  If the
	   subsystem ID matches, search that table for an entry for the given
	   command code. */

	for (i = 0; !found && i < DIAGPKT_USER_TBL_SIZE; i++)
	{
		user_tbl_entry = lg_diagpkt_user_table[i];
    
		if (user_tbl_entry != NULL && user_tbl_entry->subsysid == subsys_id && user_tbl_entry->cmd_code == cmd_code)
		{
			tbl_entry = user_tbl_entry->user_table;
      
			tbl_entry_count = (tbl_entry) ? user_tbl_entry->count : 0;
      
			for (j = 0; (tbl_entry!=NULL) && !found && j < tbl_entry_count; j++)
			{
				if (packet_id >= tbl_entry->cmd_code_lo && 
				    packet_id <= tbl_entry->cmd_code_hi)
				{
					/* If the entry has no func, ignore it. */
					if (tbl_entry->func_ptr)
					{
						found = TRUE;
						rsp_pkt = (void *) (*tbl_entry->func_ptr) (req_pkt, pkt_len);
						if (rsp_pkt)
						{
#ifdef LG_DIAG_DEBUG
							printk(KERN_ERR " LG_FW : diagpkt_process_request: about to call diagpkt_commit.\n");
#endif

							diagpkt_commit (rsp_pkt);
						} /* endif if (rsp_pkt) */
						else
						{
							{
								switch(packet_id)
								{
#ifdef CONFIG_LGE_DIAG_WMC
									case DIAG_WMCSYNC_MAPPING_F:
										break;
#endif
									default:
										lg_diag_req_pkt_ptr = req_pkt;
										lg_diag_req_pkt_length = pkt_len;
										lg_diag_app_execute();
										break;
								}
							}

						} /* endif if (rsp_pkt) */

					} /* endif if (tbl_entry->func_ptr) */
				} /* endif if (packet_id >= tbl_entry->cmd_code_lo && packet_id <= tbl_entry->cmd_code_hi)*/
				tbl_entry++;
			} /* for (j = 0; (tbl_entry!=NULL) && !found && j < tbl_entry_count; j++) */
		} /* endif if (user_tbl_entry != NULL && user_tbl_entry->subsysid == subsys_id
		     && user_tbl_entry->cmd_code == cmd_code)*/
	} /*  for (i = 0; !found && i < DIAGPKT_USER_TBL_SIZE; i++) */

  	/* Assume that rsp and rsp_pkt are NULL if !found */
  	if (!found)
	{
		//ERR_FATAL("Diag_LSM: diagpkt_process_request: Did not find match in user table",0,0,0);
		printk(KERN_ERR "LG_FW : diagpkt_process_request: Did not find match in user table \n");
	}

	/*                                            */
	/* MOD 0014110: [FACTORY RESET] stability */
	/* exception handling, merge from VS740 */
	//jihoon.lee - clear req_pkt so that current diag events do not affect the post, if it is not press and release events will be odd.
	//for example, send key followed by number of keys will be missed.
	//LG_FW khlee bug fix
	memset(req_pkt, 0, pkt_len);
	/*                                          */

  	return;
}               /* diagpkt_process_request */

static void process_diag_payload(void) 
{
	int type = *(int *)read_buffer;
	unsigned char* ptr = read_buffer+4;

#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "\n LG_FW : %s enter ,  read_buffer = 0x%x, type= %d\n", __func__,(uint32_t)read_buffer,type);
#endif

	if(type == PKT_TYPE)
		diagpkt_process_request((void*)ptr, (uint16)num_bytes_read-4, NULL, NULL);
}

static int CreateWaitThread(void* param)
{
	if(diagchar_open() != 0)
	{
#ifdef LG_DIAG_DEBUG
		printk(KERN_INFO "\n LG_FW :	size written is %d \n", driver->used);
#endif
		kthread_stop(lg_diag_thread);
		return 0; 	 
	}
#ifdef LG_DIAG_DEBUG
	printk(KERN_INFO "LG_FW : diagchar_open succeed\n");
#endif

	DIAGPKT_DISPATCH_TABLE_REGISTER(DIAGPKT_NO_SUBSYS_ID, registration_table);

	do{
		num_bytes_read = diagchar_read(read_buffer, READ_BUF_SIZE);
#ifdef LG_DIAG_DEBUG
		printk(KERN_DEBUG "LG_FW : CreateWaitThread, diagchar_read %d byte",num_bytes_read);
#endif
		if(*(int *)read_buffer == DEINIT_TYPE)
			break;
		process_diag_payload();
	}while(1);

	return 0;
}

void lgfw_diag_kernel_service_init(int driver_ptr)
{
	driver = (struct diagchar_dev*)driver_ptr;

	lg_diag_thread = kthread_run(CreateWaitThread, NULL, "kthread_lg_diag");
	if (IS_ERR(lg_diag_thread)) {
		lg_diag_thread = NULL;
		printk(KERN_ERR "LG_FW : %s: ts kthread run was failed!\n", __FUNCTION__);
		return;
	}
}
EXPORT_SYMBOL(lgfw_diag_kernel_service_init);

