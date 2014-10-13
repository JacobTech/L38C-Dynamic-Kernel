
#ifndef LG_BACKUP_ITEMS_H
#define LG_BACKUP_ITEMS_H

/*
* MISC Partition Usage
+-------+------------------------+----------+
|MISC(0~4)  |  FRST(8)  |  MEID(12)  |  NVCRC(16)  |  PID(20)  |  WLAN_MAC(24)  |  PF_NIC(28)  |  XCALBACKUP(40)
+-------+------------------------+----------+
MISC		: Recovery Message 저장용
FRST		: Factory reset flag
MEID		: NV_MEID_I backup
NVCRC		: NV crc data backup
PID			: NV_FACTORY_INFO_I backup
WLAN_MAC	: NV_WLAN_MAC_ADDRESS_I backup
PF_NIC		: NV_LG_LTE_PF_NIC_MAC_I backup
XCALBACKUP	: RF CAL Golden Copy 저장용
*/

/*
 * page offset in the partition
 */
#define PTN_FRST_PERSIST_OFFSET_IN_MISC_PARTITION 2048 
#define PTN_MEID_PERSIST_OFFSET_IN_MISC_PARTITION 12
#define PTN_NVCRC_PERSIST_OFFSET_IN_MISC_PARTITION 16
#if 1//                      
#define PTN_WLAN_MAC_PERSIST_OFFSET_IN_MISC_PARTITION  24
#define PTN_PF_NIC_MAC_PERSIST_OFFSET_IN_MISC_PARTITION  28
#endif
#define PTN_PID_PERSIST_OFFSET_IN_MISC_PARTITION  32
#define PTN_USBID_PERSIST_OFFSET_IN_MISC_PARTITION  36
#define PTN_XCAL_OFFSET_IN_MISC_PARTITION         40
/*                                            */
/* ADD 0015981: [MANUFACTURE] BACK UP MAC ADDRESS NV */
/*                  */
#define PTN_LCD_K_CAL_OFFSET_IN_MISC_PARTITION 44
#define PTN_LCD_K_CAL_PARTITION     (512*PTN_LCD_K_CAL_OFFSET_IN_MISC_PARTITION)
/*                  */
#define PTN_FRST_PERSIST_POSITION_IN_MISC_PARTITION     (512*PTN_FRST_PERSIST_OFFSET_IN_MISC_PARTITION)*7   //7Mbyte offset 
#define PTN_MEID_PERSIST_POSITION_IN_MISC_PARTITION     (512*PTN_MEID_PERSIST_OFFSET_IN_MISC_PARTITION)
#define PTN_NVCRC_PERSIST_POSITION_IN_MISC_PARTITION    (512*PTN_NVCRC_PERSIST_OFFSET_IN_MISC_PARTITION)
/*                                            */
/* ADD 0015981: [MANUFACTURE] BACK UP MAC ADDRESS NV */
#if 1//                      
#define PTN_WLAN_MAC_PERSIST_POSITION_IN_MISC_PARTITION (512*PTN_WLAN_MAC_PERSIST_OFFSET_IN_MISC_PARTITION)
#define PTN_PF_NIC_MAC_PERSIST_POSITION_IN_MISC_PARTITION  (512*PTN_PF_NIC_MAC_PERSIST_OFFSET_IN_MISC_PARTITION)
#endif
/*                                          */
#define PTN_PID_PERSIST_POSITION_IN_MISC_PARTITION      (512*PTN_PID_PERSIST_OFFSET_IN_MISC_PARTITION)
#define PTN_USBID_PERSIST_POSITION_IN_MISC_PARTITION    (512*PTN_USBID_PERSIST_OFFSET_IN_MISC_PARTITION)
#define PTN_XCAL_POSITION_IN_MISC_PARTITION             (512*PTN_XCAL_OFFSET_IN_MISC_PARTITION)


/*
 * command codes for the backup operation
 */
#define CALBACKUP_GETFREE_SPACE		0
#define CALBACKUP_CAL_READ			1
#define CALBACKUP_MEID_READ			2
#define CALBACKUP_MEID_WRITE			4
#define CALBACKUP_ERASE				5
#define CALBACKUP_CAL_WRITE			6
#define NVCRC_BACKUP_READ           		7
#define NVCRC_BACKUP_WRITE          		8
#define PID_BACKUP_READ				9
#define PID_BACKUP_WRITE				10

/*                                            */
/* ADD 0015981: [MANUFACTURE] BACK UP MAC ADDRESS NV */
#if 1//                      
#define WLAN_MAC_ADDRESS_BACKUP_READ	11
#define WLAN_MAC_ADDRESS_BACKUP_WRITE	12
#define PF_NIC_MAC_BACKUP_READ			13
#define PF_NIC_MAC_BACKUP_WRITE			14
#define USBID_REMOTE_WRITE              15
#endif
/*                                          */


#define CALBACKUP_CALFILE		0
#define CALBACKUP_MEIDFILE		1


/*
 * set magic code 8 byte for validation
 */
#define MEID_MAGIC_CODE 0x4D4549444D454944 /*MEIDMEID*/
#define MEID_MAGIC_CODE_SIZE 8

#define FACTORYINFO_MAGIC_CODE 0x5049445F /*PID_*/
#define FACTORYINFO_MAGIC_CODE_SIZE 4

#define WLAN_MAGIC_CODE 0x574C /*WL*/
#define PFNIC_MAGIC_CODE 0x5046 /*PF*/
#define MACADDR_MAGIC_CODE_SIZE 2

#define MEIDBACKUP_SECTOR_SIZE		1

#define USBID_MAGIC_CODE 0x55534249 /*USBI*/
#define USBID_MAGIC_CODE_SIZE 4

/*
 * actual item size declaration, smem allocation size should be 64-bit aligned
 */
//#define MEIDBACKUP_BYTES_SIZE				8
#define MEID_BACKUP_SIZE					8
#define MEID_BACKUP_64BIT_ALIGNED_SIZE	(MEID_MAGIC_CODE_SIZE+MEID_BACKUP_SIZE)

#define FACTORYINFO_BACKUP_SIZE					100
#define FACTORYINFO_BACKUP_64BIT_ALIGNED_SIZE	(FACTORYINFO_MAGIC_CODE_SIZE + FACTORYINFO_BACKUP_SIZE)


/*                                            */
/* ADD 0015981: [MANUFACTURE] BACK UP MAC ADDRESS NV */
#if 1//                      
#define MACADDR_BACKUP_SIZE					6
#define MACADDR_BACKUP_64BIT_ALIGNED_SIZE	(MACADDR_MAGIC_CODE_SIZE + MACADDR_BACKUP_SIZE)
#endif
/*                                          */

//1 2011.02.25 current smem_alloc size is 24836
#define BACKUP_TOTAL_SIZE 24836

#endif /* LG_BACKUP_ITEMS_H */


