/**
 * User space driver API for chipsailing's fingerprint device.
 * ATTENTION: Do NOT edit this file unless the corresponding driver changed.
 *
 * Copyright (C) 2016 chipsailing Corporation. <http://www.chipsailing.com>
 * Copyright (C) 2016 XXX <mailto:xxx@chipsailing.com>
 *
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by the Free 
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General 
 * Public License for more details.
**/

#include <linux/spi/spi.h>
#include "cf_ctl.h"

#define MTK_SPI_ALIGN_MASK_NUM  10
#define MTK_SPI_ALIGN_MASK  ((0x1 << MTK_SPI_ALIGN_MASK_NUM) - 1)
#define	SPI_BUFSIZ	32


static void cf_spi_complete(void *arg)
{
	complete(arg);
}

DEFINE_SPINLOCK(mr_lock);
static int cf_sync_read_and_write(struct spi_device *spi, struct spi_message *message)
{
	
	DECLARE_COMPLETION_ONSTACK(done);	
	int rc;

	if (NULL == spi || NULL == message) {
		pr_err("chipsailing: invalid arguments\n");
		return -EINVAL;
	}
	message->complete = cf_spi_complete;
	message->context = &done;

	spin_lock_irq(&mr_lock);
	if (NULL == spi) {
		rc = -ESHUTDOWN;
		pr_err("chipsailing: device is null. \n");
		goto exit;
	}
	rc = spi_async(spi, message);	
	spin_unlock_irq(&mr_lock);

	if (rc) {
		pr_err("chipsailing: fail to async message, error = %d", rc);
		goto exit;
	}
	
	wait_for_completion(&done);
	rc = message->status;
		
exit:
	return rc;
}
 

static unsigned char buf[SPI_BUFSIZ] = {0};

static int cf_spi_read_and_write(struct spi_device *spi, void *txbuf, unsigned n_tx, void *rxbuf, unsigned n_rx)
{
	static DEFINE_MUTEX(lock);

	int			rc;
	struct spi_message	message;
	//struct spi_transfer	x[2];
	struct spi_transfer x = {0};  
	unsigned char *local_buf;
	uint32_t package_num = 0;
	uint32_t remainder = 0;
	uint32_t packet_size = 0;

	/* Use preallocated DMA-safe buffer if we can.  We can't avoid
	 * copying here, (as a pure convenience thing), but we can
	 * keep heap costs out of the hot path unless someone else is
	 * using the pre-allocated buffer or the transfer is too large.
	 */
	if (NULL == spi){
		pr_err("chipsailing: invalid argument\n");
		rc = -EINVAL;
		goto exit;
	} 
	
	
#if defined(MTK_PLATFORM)
	#if 0//(!defined(MTK6739) || !defined(KERNEL49))
	/*switch to DMA if bytes larger than 32*/
	if ((n_tx + n_rx +1) > 32)
		cf_spi_setup(spi, 0, DMA_TRANSFER);
	#endif
#endif

    package_num = (n_tx + n_rx) >> MTK_SPI_ALIGN_MASK_NUM;
	remainder = (n_tx + n_rx) & MTK_SPI_ALIGN_MASK;
	if ((package_num > 0) && (remainder != 0)) {
		packet_size = ((package_num + 1) << MTK_SPI_ALIGN_MASK_NUM);
	} else {
		packet_size = n_tx + n_rx;
	}
	
	if (packet_size > SPI_BUFSIZ || !mutex_trylock(&lock)) {
		local_buf = kmalloc(max((unsigned)SPI_BUFSIZ, packet_size),GFP_KERNEL);  
		if (NULL == local_buf){
			pr_err("chipsailing: short of mem\n");
			rc =  -ENOMEM;
			goto exit;
		}
	} else {
		local_buf = buf;
	}
	
	spi_message_init(&message);
	
	//initialize spi_transfer
	memset(&x,0,sizeof(x));
	memcpy(local_buf,txbuf,n_tx);
	
	x.cs_change = 0;
	x.delay_usecs = 1;
	x.speed_hz = 3000000;
	x.tx_buf = local_buf;
	x.rx_buf = local_buf;
	x.len = packet_size;

	spi_message_add_tail(&x, &message);
	rc = cf_sync_read_and_write(spi,&message);
	if (rc) {
		pr_err("chipsailing: fail to sync message, error = %d", rc);
		//goto exit;
	}
	memcpy(rxbuf,local_buf+n_tx,n_rx);
	
	
#if defined(MTK_PLATFORM)
	#if 0//(!defined(MTK6739) || !defined(KERNEL49))
	/*switch back to FIFO after DMA transfer*/
	if ((n_tx + n_rx +1) > 32)
	    cf_spi_setup(spi, 0, FIFO_TRANSFER);
	#endif
#endif 

	if (x.tx_buf == buf) {
		mutex_unlock(&lock);
	} else {
		kfree(local_buf);
	}

exit:	
	return rc;
}


int cf_sfr_read(struct spi_device *spi, unsigned short addr, unsigned char *recv_buf, unsigned short buflen)
{
	int rc = -1;
	unsigned char tx_buf[2] = {0};

	tx_buf[0] = CHIPS_R_SFR;
	tx_buf[1] = (unsigned char)(addr & 0x00FF);

	rc = cf_spi_read_and_write(spi, tx_buf, 2, recv_buf, buflen);
	if (rc) {
		pr_err("chipsailing: fail to read sfr,error = %d", rc);
	}
	
	return rc;
}


int cf_sfr_write(struct spi_device *spi, unsigned short addr, unsigned char *send_buf, unsigned short buflen)
{
	
	unsigned char *tx_buf = NULL;
	int rc;

	tx_buf = (unsigned char *)kmalloc(buflen+2,GFP_KERNEL);
	if (NULL == tx_buf) {
		pr_err("chipsailing: short of mem\n");
		rc = -ENOMEM;
		goto exit;
	}
	
	tx_buf[0] = CHIPS_W_SFR;
	tx_buf[1] = (unsigned char)(addr & 0x00FF);
	memcpy(tx_buf+2,send_buf,buflen);

	rc = cf_spi_read_and_write(spi,tx_buf,buflen+2,NULL,0);
	if (rc) {
		pr_err("chipsailing: cf_spi_read_and_write fail,error = %d\n", rc);
	}

exit:	
	if (NULL != tx_buf) {
	    kfree(tx_buf);
		tx_buf = NULL;
	}
	
	return rc;
}


int cf_sram_read(struct spi_device *spi, unsigned short addr, unsigned char *recv_buf, unsigned short buflen)
{
	unsigned char tx_buf[3] = {0};
	int rc;
	
	unsigned char *rx_buf = NULL;
	rx_buf = (unsigned char *)kmalloc(buflen+1, GFP_KERNEL);   //first nop
	if(NULL == rx_buf){
		pr_err("chipsailing: short of mem\n");
		rc = -ENOMEM;
		goto exit;
	}
	
	tx_buf[0] = CHIPS_R_SRAM;
	tx_buf[1] = (unsigned char)((addr&0xFF00)>>8);
	tx_buf[2] = (unsigned char)(addr&0x00FF);
	
	rc = cf_spi_read_and_write(spi, tx_buf, 3, rx_buf, buflen+1);
	if (rc) {
		pr_err("chipsailing: cf_spi_read_and_write fail, error = %d", rc);
		goto exit;
	}
		
	memcpy(recv_buf, rx_buf+1, buflen);	
	
exit:
	if (NULL != rx_buf) {
		kfree(rx_buf);
		rx_buf = NULL;
	}
	
	return rc;
}



int cf_sram_write(struct spi_device *spi, unsigned short addr, unsigned char *send_buf, unsigned short buflen)
{ 
	unsigned char *tx_buf = NULL;
	int rc;
	
	tx_buf = (unsigned char *)kmalloc(buflen+3,GFP_KERNEL);
	if(NULL == tx_buf){
		pr_err("chipsailing: short of mem\n");
		rc = -ENOMEM;
		goto exit;
	}

	tx_buf[0] = CHIPS_W_SRAM;
	tx_buf[1] = (unsigned char)((addr&0xFF00)>>8);
	tx_buf[2] = (unsigned char)(addr&0x00FF);        
	memcpy(tx_buf+3, send_buf, buflen);

	rc = cf_spi_read_and_write(spi, tx_buf, buflen+3, NULL, 0);
	if (rc) {
		pr_err("chipsailing: cf_spi_read_and_write fail, error = %d", rc);
	}
	
exit:

	if (NULL != tx_buf) {
		kfree(tx_buf);
		tx_buf = NULL;
	}
	
	return rc;
}



int cf_spi_cmd(struct spi_device *spi, unsigned char *cmd, unsigned short cmdlen)
{
	int rc;
	rc = cf_spi_read_and_write(spi, cmd, cmdlen, NULL, 0);
	if (rc) {
		pr_err("chipsailing: cf_spi_read_and_write fail, error = %d", rc);
	}
	
	return rc;
}


int cf_write_configs(struct spi_device *spi, struct param *p_param, int num)
{
	struct param param;
	unsigned char val;
	int i = 0;
	int rc = 0;
	unsigned char tx_buf[2] = {0};
	
	for(i = 0; i < num; i++)
	{
		param = p_param[i];
		
		if (param.cmd == CHIPS_W_SFR) {
			val = (unsigned char)(param.data&0x00FF);
			rc = cf_sfr_write(spi, param.addr, &val, 1);
			if (rc) {
				pr_err("chipsailing: cf_sfr_write fail, error = %d", rc);
				goto exit;
			}
			pr_err("chipsailing: param.cmd = %x,param.addr = %x,param.data = %x\n", param.cmd, param.addr, val);
		} else if (param.cmd == CHIPS_W_SRAM) {
		    tx_buf[0] = (unsigned char)(param.data&0x00FF);  //low 8 bits
	        tx_buf[1] = (unsigned char)((param.data&0xFF00)>>8);  //high 8 bits
			rc = cf_sram_write(spi, param.addr, tx_buf, 2);
			if (rc) {
				pr_err("chipsailing: cf_sfr_write fail, error = %d", rc);
				goto exit;
			}
			pr_info("chipsailing: param.cmd = %x,param.addr = %x,param.data = %x\n", param.cmd, param.addr,  param.data);
		}
	}

exit:	
	return rc;
}





