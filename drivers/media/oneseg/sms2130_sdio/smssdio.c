/*
 *  smssdio.c - Siano 1xxx SDIO interface driver
 *
 *  Copyright 2008 Pierre Ossman
 *
 * Based on code by Siano Mobile Silicon, Inc.,
 * Copyright (C) 2006-2008, Uri Shkolnik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 *
 * This hardware is a bit odd in that all transfers should be done
 * to/from the SMSSDIO_DATA register, yet the "increase address" bit
 * always needs to be set.
 *
 * Also, buffers from the card are always aligned to 128 byte
 * boundaries.
 */

/*
 * General cleanup notes:
 *
 * - only typedefs should be name *_t
 *
 * - use ERR_PTR and friends for smscore_register_device()
 *
 * - smscore_getbuffer should zero fields
 *
 * Fix stop command
 */

#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/host.h>

#include "smscoreapi.h"
#include "sms-cards.h"

/* Registers */
#define SMSSDIO_DATA		0x00
#define SMSSDIO_INT		0x04
#define SMSSDIO_BLOCK_SIZE	128

#define SMS_READ_USING_STATIC_BUF
#define SMS_READ_BUF_SIZE 0x5000
#ifdef SMS_READ_USING_STATIC_BUF
static unsigned char gBuffer[SMS_READ_BUF_SIZE];
#else
unsigned char *gBuffer = NULL;
#endif

static const struct sdio_device_id smssdio_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_STELLAR),
	 .driver_data = SMS1XXX_BOARD_SIANO_STELLAR},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_A},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_NOVA_B0),
	 .driver_data = SMS1XXX_BOARD_SIANO_NOVA_B},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VEGA_A0),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, SDIO_DEVICE_ID_SIANO_VENICE),
	 .driver_data = SMS1XXX_BOARD_SIANO_VEGA},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x302),
	 .driver_data = SMS1XXX_BOARD_SIANO_MING},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x500),
	 .driver_data = SMS1XXX_BOARD_SIANO_PELE},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x600),
	 .driver_data = SMS1XXX_BOARD_SIANO_RIO},
    {SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x700),
	 .driver_data = SMS1XXX_BOARD_SIANO_DENVER_2160},
	{SDIO_DEVICE(SDIO_VENDOR_ID_SIANO, 0x800),
	 .driver_data = SMS1XXX_BOARD_SIANO_DENVER_1530},
	 { /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(sdio, smssdio_ids);

struct smssdio_device {
	struct sdio_func *func;

	struct smscore_device_t *coredev;

	struct smscore_buffer_t *split_cb;
};

/*******************************************************************/
/* Siano core callbacks                                            */
/*******************************************************************/

static int smssdio_sendrequest(void *context, void *buffer, size_t size)
{
	int ret;
	struct smssdio_device *smsdev;

	smsdev = context;

	//printk("[%s] In~!!\n",__func__);


	sdio_claim_host(smsdev->func);

	printk("[%s] smssdio_sendrequest~!!\n",__func__);
	
	while (size >= smsdev->func->cur_blksize) {
			//printk("[%s] size=%d, cur_blksize=%d!!\n",__func__, size, smsdev->func->cur_blksize);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA, buffer, smsdev->func->cur_blksize);
#else
		ret = sdio_write_blocks(smsdev->func, SMSSDIO_DATA, buffer, 1);
#endif
		if (ret)
			goto out;

		buffer += smsdev->func->cur_blksize;
		size -= smsdev->func->cur_blksize;
	}

	if (size) {
		//printk("[%s] size=%d !!\n",__func__, size);
		
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
		ret = sdio_memcpy_toio(smsdev->func, SMSSDIO_DATA, buffer, size);
#else
		ret = sdio_write_bytes(smsdev->func, SMSSDIO_DATA,
				       buffer, size);
#endif
	//printk("[%s] sdio_memcpy_toio ret=%d !!\n",__func__, ret);

	}

out:
	sdio_release_host(smsdev->func);

	//printk("[%s] Out~!!\n",__func__);

	return ret;
}

/*******************************************************************/
/* SDIO callbacks                                                  */
/*******************************************************************/

static void smssdio_interrupt(struct sdio_func *func)
{
	int ret, isr;

	struct smssdio_device *smsdev;
	struct smscore_buffer_t *cb;
	struct SmsMsgHdr_ST *hdr;
	size_t size;

	//printk("[Shawn][%s] interrupt~!!\n",__func__);
	//sms_err("[Shawn][%s] interrupt~!!\n",__func__);
	smsdev = sdio_get_drvdata(func);

	/*
	 * The interrupt register has no defined meaning. It is just
	 * a way of turning of the level triggered interrupt.
	 */
	isr = sdio_readb(func, SMSSDIO_INT, &ret);
	if (ret) {
		sms_err("Unable to read interrupt register!\n");
		//printk("Unable to read interrupt register!\n");
		return;
	}
//	printk("smssdio_interrupt 1\n");

	if (smsdev->split_cb == NULL) {
		cb = smscore_getbuffer(smsdev->coredev);
		if (!cb) {
			sms_err("Unable to allocate data buffer!\n");
			//printk("Unable to allocate data buffer!\n");
			return;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
#if 0//                                    

#if 0
		ret = sdio_memcpy_fromio(smsdev->func,
					 cb->p,
					 SMSSDIO_DATA,
					 SMSSDIO_BLOCK_SIZE);
#endif
			{
				int i=0;
				unsigned char *tmp;
				tmp = (unsigned char*)cb->p;
				for(i=0;i<SMSSDIO_BLOCK_SIZE;i++)
					{
					*tmp = sdio_readb(func, SMSSDIO_DATA, &ret);

					if(!(i%16))printk("\n");
					printk("0x%02x ",*tmp++);
					}
				
					printk("\n");
				
			}

#else
#ifdef SMS_READ_USING_STATIC_BUF
#else
	if(gBuffer == NULL)return;
#endif
	ret = sdio_memcpy_fromio(smsdev->func,
				 gBuffer/*cb->p*/,
				 SMSSDIO_DATA,
				 SMSSDIO_BLOCK_SIZE);

	memcpy((char*)cb->p,gBuffer,SMSSDIO_BLOCK_SIZE);

#endif
#else
		ret = sdio_read_blocks(smsdev->func, cb->p, SMSSDIO_DATA, 1);
#endif
		if (ret) {
			sms_err("Error %d reading initial block!\n", ret);
			//printk("Error %d reading initial block!\n", ret);
			return;
		}

		hdr = cb->p;

		if (hdr->msgFlags & MSG_HDR_FLAG_SPLIT_MSG) {
			smsdev->split_cb = cb;
			return;
		}

		if (hdr->msgLength > smsdev->func->cur_blksize)
			size = hdr->msgLength - smsdev->func->cur_blksize;
		else
			size = 0;
	} else {
		cb = smsdev->split_cb;
		hdr = cb->p;

		size = hdr->msgLength - sizeof(struct SmsMsgHdr_ST);

		smsdev->split_cb = NULL;
	}

	if (size) {
		void *buffer;

		buffer = cb->p + (hdr->msgLength - size);
		size = ALIGN(size, SMSSDIO_BLOCK_SIZE);

		BUG_ON(smsdev->func->cur_blksize != SMSSDIO_BLOCK_SIZE);

		/*
		 * First attempt to transfer all of it in one go...
		 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
#if 0//                                    
	ret = sdio_memcpy_fromio(smsdev->func,
				 cb->p,
				 SMSSDIO_DATA,
				 size);
#else
#ifdef SMS_READ_USING_STATIC_BUF
#else
	if(gBuffer == NULL)return;
#endif
	ret = sdio_memcpy_fromio(smsdev->func,
				 gBuffer/*cb->p*/,
				 SMSSDIO_DATA,
				 size);
	memcpy(buffer,gBuffer,size);
#endif	
#else
		ret = sdio_read_blocks(smsdev->func, buffer,
				        SMSSDIO_DATA, size/SMSSDIO_BLOCK_SIZE);
#endif
		if (ret && ret != -EINVAL) {
			smscore_putbuffer(smsdev->coredev, cb);
			sms_err("Error %d reading data from card!\n", ret);
			//printk("Error %d reading data from card!\n", ret);
			return;
		}

		/*
		 * ..then fall back to one block at a time if that is
		 * not possible...
		 *
		 * (we have to do this manually because of the
		 * problem with the "increase address" bit)
		 */
		if (ret == -EINVAL) {
			while (size) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 10)
#if 0
				ret = sdio_memcpy_fromio(smsdev->func,
							 cb->p,
							 SMSSDIO_DATA,
							 smsdev->func->cur_blksize);
#else
#ifdef SMS_READ_USING_STATIC_BUF
#else
				if(gBuffer == NULL)return;
#endif
				ret = sdio_memcpy_fromio(smsdev->func,
							 gBuffer/*cb->p*/,
							 SMSSDIO_DATA,
							 smsdev->func->cur_blksize);
				memcpy(buffer,gBuffer,smsdev->func->cur_blksize);
#endif				
#else
				ret = sdio_read_blocks(smsdev->func,
							buffer,
							SMSSDIO_DATA,
							1);
#endif
				if (ret) {
					smscore_putbuffer(smsdev->coredev, cb);
					sms_err("Error %d reading "
						"data from card!\n", ret);
					//printk("Error %d reading "
					//	"data from card!\n", ret);
					return;
				}
				buffer += smsdev->func->cur_blksize;
				if (size > smsdev->func->cur_blksize)
					size -= smsdev->func->cur_blksize;
				else
					size = 0;
			}
		}
	}

	cb->size = hdr->msgLength;
	cb->offset = 0;

	smscore_onresponse(smsdev->coredev, cb);
	//printk("interrupt out\n");
}

void Log_message(struct sdio_func *func)
{
   int i;

   printk("==========================================================================\n");
   printk("Function Number      = %d\n", func->num);
   printk("Class                = %d\n", func->class);
   printk("Vendor               = 0x%04X\n", func->vendor);
   printk("Device               = 0x%04X\n", func->device);
   printk("Max Block Size       = %d\n", func->max_blksize);
   printk("Current Block Size   = %d\n", func->cur_blksize);
   printk("Function State       = %d\n", func->state);
   printk("Tmpbuf[4]            = 0x%02X, 0x%02X, 0x%02X, 0x%02X\n", func->tmpbuf[0], func->tmpbuf[1], func->tmpbuf[2], func->tmpbuf[3]);
   printk("num of Info string   = %d\n", func->num_info);
   for(i=0; i<func->num_info;i++) {
      printk("   Info String[%d]  = %s\n", i, func->info[i]);
   }
   printk("---------------------------- [ CARD ] ----------------------------------------------\n");
   printk("RCA                  = %d\n", func->card->rca);
   printk("Type                 = %d(0:MMC, 1:SD, 2:SDIO)\n", func->card->type);
   printk("State                = %d\n", func->card->state);
   printk("CID                  = [0x%02X, 0x%02X, 0x%02X, 0x%02X]\n", func->card->raw_cid[0], func->card->raw_cid[1],
                                                                       func->card->raw_cid[2], func->card->raw_cid[3]);
   printk("CSD                  = [0x%02X, 0x%02X, 0x%02X, 0x%02X]\n", func->card->raw_csd[0], func->card->raw_csd[1],
                                                                       func->card->raw_csd[2], func->card->raw_csd[3]);
   printk("SCR                  = [0x%02X, 0x%02X, 0x%02X, 0x%02X]\n", func->card->raw_scr[0], func->card->raw_scr[1],
                                                                       func->card->raw_scr[2], func->card->raw_scr[3]);
   printk("Number of SDIO func. = %d\n", func->card->sdio_funcs);
   printk("--------------------------- [ HOST ] ----------------------------------------------\n");
   printk("Index                = %d\n", func->card->host->index);
   printk("f_min                = %d\n", func->card->host->f_min);
   printk("f_max                = %d\n", func->card->host->f_max);
   printk("ocr_avail(Power)     = %X\n", func->card->host->ocr_avail);
   printk("Host capabilities    = %lu\n", func->card->host->caps);
   printk("--------------------------- [ IOS ] ----------------------------------------------\n");
   printk("Clock                = %d\n", func->card->host->ios.clock);
   printk("VDD                  = %d\n", func->card->host->ios.vdd);
   printk("Bus Mode             = %d(1 : Open Drain, 2 : Push/Pull)\n", func->card->host->ios.bus_mode);
   printk("Chips Select         = %d(0 : Don't care, 1 : High, 2 : Low)\n", func->card->host->ios.chip_select);
   printk("Power Mode           = %d(0 : Power Off, 1 : Power Up, 2 : Power On)\n", func->card->host->ios.power_mode);
   printk("Bus Width            = %d(0 : 1bit, 2 : 4bits)\n", func->card->host->ios.bus_width);
   printk("Timing               = %d(0 : Legacy, 1 : MMC-HS, 2 : SD-HS)\n", func->card->host->ios.timing);
   printk("==========================================================================\n");
}
//#include "../../mtd/devices/msm_nand.h"
static int smssdio_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	int ret;

	int board_id;
	struct smssdio_device *smsdev;
	struct smsdevice_params_t params;

	board_id = id->driver_data;

	printk("[Shawn][%s] Probe start~!\n",__func__);
	smsdev = kzalloc(sizeof(struct smssdio_device), GFP_KERNEL);
	if (!smsdev)
		return -ENOMEM;

	smsdev->func = func;

	memset(&params, 0, sizeof(struct smsdevice_params_t));

	params.device = &func->dev;
	params.buffer_size = 0x2000; //0x5000;	/* ?? */
	params.num_buffers = 5;//22;	/* ?? */
	params.context = smsdev;

	snprintf(params.devpath, sizeof(params.devpath),
		 "sdio\\%s", sdio_func_id(func));

	params.sendrequest_handler = smssdio_sendrequest;

	params.device_type = sms_get_board(board_id)->type;

	if (params.device_type != SMS_STELLAR)
		params.flags |= SMS_DEVICE_FAMILY2;
	else {
		/*
		 * FIXME: Stellar needs special handling...
		 */
		ret = -ENODEV;
		goto free;
	}

	ret = smscore_register_device(&params, &smsdev->coredev);
	if (ret < 0)
		goto free;

	smscore_set_board_id(smsdev->coredev, board_id);

	sdio_claim_host(func);

	Log_message(func);

	ret = sdio_enable_func(func);
	if (ret)
		goto release;

	ret = sdio_set_block_size(func, SMSSDIO_BLOCK_SIZE);
	if (ret)
		goto disable;

	ret = sdio_claim_irq(func, smssdio_interrupt);
	if (ret)
		goto disable;

	sdio_set_drvdata(func, smsdev);

	sdio_release_host(func);

	printk("[Shawn][%s] Probe end\n",__func__);

	//mdelay(5000);

	ret = smscore_start_device(smsdev->coredev);
	if (ret < 0)
		goto reclaim;
	
	printk("[Shawn][%s] Probe end2\n",__func__);

	//printk("[CHANG] EBI CS_CFG(0x%x) = 0x%x\n",(unsigned int)EBI2_CHIP_SELECT_CFG0,readl(EBI2_CHIP_SELECT_CFG0));
	//printk("[CHANG] EBI CFG(0x%x) = 0x%x\n",(unsigned int)EBI2_CFG_REG,readl(EBI2_CFG_REG));
	return 0;

reclaim:
	printk("[Shawn][%s] Probe reclaim\n",__func__);
	sdio_claim_host(func);
	sdio_release_irq(func);
disable:
	sdio_disable_func(func);
release:
	sdio_release_host(func);
	smscore_unregister_device(smsdev->coredev);
free:
	kfree(smsdev);

	return ret;
}



static void smssdio_remove(struct sdio_func *func)
{
	struct smssdio_device *smsdev;

	smsdev = sdio_get_drvdata(func);

	/* FIXME: racy! */
	if (smsdev->split_cb)
		smscore_putbuffer(smsdev->coredev, smsdev->split_cb);

	smscore_unregister_device(smsdev->coredev);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	kfree(smsdev);
}

static struct sdio_driver smssdio_driver = {
	.name = "smssdio",
	.id_table = smssdio_ids,
	.probe = smssdio_probe,
	.remove = smssdio_remove,
};

/*******************************************************************/
/* Module functions                                                */
/*******************************************************************/

int smssdio_register(void)
{
	int ret = 0;

	printk(KERN_INFO "smssdio: Siano SMS1xxx SDIO driver\n");
	printk(KERN_INFO "smssdio: Copyright Pierre Ossman\n");

	ret = sdio_register_driver(&smssdio_driver);

#ifdef SMS_READ_USING_STATIC_BUF
#else
	gBuffer = kmalloc(0x5000,GFP_KERNEL|GFP_DMA);
#endif
	return ret;
}

void smssdio_unregister(void)
{
	if(gBuffer != NULL)kfree(gBuffer);
	sdio_unregister_driver(&smssdio_driver);
}

MODULE_DESCRIPTION("Siano SMS1xxx SDIO driver");
MODULE_AUTHOR("Pierre Ossman");
MODULE_LICENSE("GPL");
