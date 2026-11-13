/*
 * DOSCOM Multi Domain Communications Driver for Linux
 *
 * Copyright (C) Nagoya University
 * Daniel Sangorrin <daniel.sangorrin@gmail.com>
 * Shinya Honda     <honda@ertl.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  @(#) $Id: doscom.c 52 2020-05-29 00:46:12Z aicp-suzuki $
 */
#include <linux/kernel.h>
#include <linux/module.h>      /* for MODULE_AUTHOR..*/
#include <linux/init.h>        /* for module_init */
#include <asm/io.h>            /* for virt_to_phys, ioremap */
#include <linux/semaphore.h>
#include <linux/wait.h>        /* for wait queues */
#include <linux/sched.h>
#include <linux/interrupt.h>   /* for request_irq */
#include <linux/fs.h>          /* for alloc_chrdev_region */
#include <linux/cdev.h>        /* for cdev_alloc */
#include <linux/dma-mapping.h> /* for dma_alloc_coherent */
#include <linux/ioctl.h>
#include <asm/bitops.h>        /* for test_and_set_bit */
#include <linux/slab.h>        /* for kmalloc/kfree */
#include <linux/uaccess.h>     /* for copy_to/from_user */
#include <linux/miscdevice.h>  /* for misc_register */
#include <linux/delay.h>
#include "doscom_target_driver.h"  /* for shared memory and interrupts */
#include "doscom_driver.h"  /* for shared memory and interrupts */

/*
 * DEBUG
 */
#ifndef DEBUG
#define DEBUG(enable,x,args...)  \
		if(enable) printk(KERN_INFO "" x, ##args)
#endif  /* DEBUG */

#define DBG_INIT		false
#define DBG_OPEN		false
#define DBG_CALLS		false
#define DBG_REGFIFOCH	false
#define DBG_NOTIFY		false
#define DBG_RELEASE		false
#define DBG_R2MREQ		false
#define DBG_IRQ			false

/*
 * MAX VALUES (can be adjusted)
 */
#define MAX_NUM_FIFOCHS			32
#define MAX_NUM_PROCESS			32
#define MAX_FIFOCH_PENDING_OFFSET	(SREGION_SIZE - MAX_NUM_FIFOCHS*8)

/*
 * CONTROL OFFSET
 */
#define CTL_M2R_REQ				0x00
#define CTL_R2M_REQ				0x04
#define CTL_R2M_REQ_ARG			0x08

/*
 * CONTROL VAL
 */
#define CTL_R2M_REQ_NONE		0x00
#define CTL_R2M_REQ_PROCESSEXIT	0x01
#define CTL_R2M_REQ_NOTIFY		0x02
#define CTL_R2M_REQ_PROCESSOPEN	0x03
#define CTL_R2M_REQ_REGFIFOCH	0x04
#define CTL_R2M_REQ_INITFIFOCH	0x05
#define CTL_R2M_REQ_INIT		0x87654321
#define CTL_M2R_REQ_NONE		0x00
#define CTL_M2R_REQ_INIT		0x12345678
#define CTL_M2R_REQ_ACK			0x06

/* Master acknowledge timeout time (uint : 100[us]) */
#define R2M_REQ_TIMEOUT			50000

/*
 * CONTROL MACRO
 */
#define set_r2m_req(cmd)		(*(volatile uint32_t *)(doscom_sregion + CTL_R2M_REQ) = cmd)
#define get_m2r_req()			(*(volatile uint32_t *)(doscom_sregion + CTL_M2R_REQ))
#define get_r2m_req()			(*(volatile uint32_t *)(doscom_sregion + CTL_R2M_REQ))
#define set_r2m_req_arg(arg)	(*(volatile uint32_t *)(doscom_sregion + CTL_R2M_REQ_ARG) = arg)
#define get_r2m_req_arg()		(*(volatile uint32_t *)(doscom_sregion + CTL_R2M_REQ_ARG))

/*
 * FIFOCH M2R request
 */
#define FIFOCH_M2R_NONE    0
#define FIFOCH_M2R_NOFITY  1
#define FIFOCH_M2R_INIT    2

/*
 * FIFOCH R2M request
 */
#define FIFOCH_R2M_NONE    0
#define FIFOCH_R2M_NOFITY  1
#define FIFOCH_R2M_INIT    2

/*
 *  CHECK_MACRO
 */
#define CHECK_INITIALIZED() do {				\
	if (!initialized) {							\
		return -ENOTTY; /* DOSCOM_NOINIT */		\
	}											\
} while(false)

#define CHECK_M_REQ_INITIALIZED() do {			\
	if (get_m2r_req() != CTL_M2R_REQ_INIT) {		\
		printk(KERN_INFO "CHECK_M_REQ_INITIALIZED failed\n"); \
		return -EAGAIN; /* DOSCOM_EINIT */			\
	}												\
} while(false)

#define CHECK_FIFOCH_ID(id) do {					\
	if (id >= the_doscom_kernel.num_fifochs) {		\
		return -EINVAL; /* DOSCOM_EPARAM */			\
	}												\
} while(false)

#define CHECK_OWN_FIFOCH(id) do {							\
	if (the_doscom_kernel.fifoch_tgid[id] != current->tgid){	\
		return -EACCES; /*  DOSCOM_EASSIGN */				\
	}														\
} while(false)

/*
 * MODULE TYPES
 */
typedef struct {
	uint32_t			id;				// fifo channel id
	uint32_t			*m2r_pending;	// pointer to shared regsion
	uint32_t			*r2m_pending;	// pointer to shared regsion
	uint32_t			wercd;			// wait ercd
	wait_queue_head_t	fifoch_wq;		// wait queue for channels
} doscom_fifoch_event_t;

typedef struct {
	uint32_t				num_fifochs;					// number of fifo channels
	doscom_fifoch_event_t	*fifoch_events;					// array of channel events
	uint32_t				fifoch_mask;					// mask of assigned channel
	pid_t					fifoch_tgid[MAX_NUM_FIFOCHS];	// tgid of each channel
	uint32_t				num_process;					// number of processs to connect this driver
	bool					r2m_req_ack;					//
	wait_queue_head_t		r2m_req_ack_wq;					// wait queue for channels
	pid_t					lockd_tgid;						//
} doscom_kernel_t;

/*
 * MODULE VARIABLE DECLARATIONS
 *	  initialized: flag to indicate if ioctl init was already called
 *	  the_doscom_kernel_sem: semaphore used to protect the driver
 *	  the_doscom_kernel_mutex: semaphore used to protect the native mutex
 *	  doscom_sregion: shared memory pointer (kernel virtual address)
 *	  the_doscom_kernel: big structure containing all the information
 *	  major: device major number (necessary when unregistering the device)
 *	  doscom_cdev: character device (necessary when removing the device)
 */
static bool initialized = false;
static DEFINE_SEMAPHORE(the_doscom_kernel_sem);
void 	*doscom_sregion;
static doscom_kernel_t the_doscom_kernel;

#ifdef ALLOC_DEVICE_MANUALLY
static int major;
static struct cdev doscom_cdev;
#endif /* ALLOC_DEVICE_MANUALLY */

static void
send_r2m_req(uint32_t req_no, uint32_t arg)
{
	DEBUG(DBG_R2MREQ, "Send R2M Request no 0x%08x, arg 0x%x\n", req_no, arg);
	set_r2m_req(req_no);
	set_r2m_req_arg(arg);

	init_waitqueue_head(&(the_doscom_kernel.r2m_req_ack_wq));
	the_doscom_kernel.r2m_req_ack = false;
	_target_int_send();
	wait_event_interruptible(the_doscom_kernel.r2m_req_ack_wq,
							 the_doscom_kernel.r2m_req_ack == true);
}

static void
send_r2m_req_noresp(uint32_t req_no, uint32_t arg)
{
	DEBUG(DBG_R2MREQ, "Send R2M Request(no response interrupt) no 0x%08x, arg 0x%x\n", req_no, arg);
	set_r2m_req(req_no);
	set_r2m_req_arg(arg);
	_target_int_send();
}

static int
wait_complete_r2m_req(int tout_100us)
{
	int		tcnt = 0;

	if( tout_100us > 0 )  {
		while( get_r2m_req() != CTL_M2R_REQ_NONE )  {
			if( tcnt < tout_100us )  {
				usleep_range(100, 200);
				tcnt++;
			}
			else  {
				return DOSCOM_BUSY;
			}
		}
	}
	else if( tout_100us == 0 )  {
		if( get_r2m_req() != CTL_M2R_REQ_NONE )  {
			return DOSCOM_BUSY;
		}
	}
	else  {
		while( get_r2m_req() != CTL_M2R_REQ_NONE );
	}

	return DOSCOM_SUCCESS;
}

static int
doscom_trylock(void)
{
	long ret;

	CHECK_INITIALIZED();

	ret = _target_lock_trylock();

	if (ret == DOSCOM_SUCCESS) {
		the_doscom_kernel.lockd_tgid = current->tgid;
	}

	return ret;
}

static int
doscom_unlock(void)
{
	long ret;

	CHECK_INITIALIZED();

	ret = _target_lock_unlock();

	if (ret == DOSCOM_SUCCESS) {
		the_doscom_kernel.lockd_tgid = 0;
	}

	return ret;
}

/*
 * OPEN
 */
static int
doscom_open(struct inode *inode, struct file *filp)
{
	int	ret;

	DEBUG(DBG_OPEN, "%s: begin (sregion=0x%p)\n", __FUNCTION__, doscom_sregion);

	if (!initialized) {
		CHECK_M_REQ_INITIALIZED();
	}

	down(&the_doscom_kernel_sem);

	DEBUG(DBG_OPEN, "OPEN called\n");

	if (the_doscom_kernel.num_process >= MAX_NUM_PROCESS) {
		DEBUG(DBG_INIT, "Number of proess is over.\n");
		up(&the_doscom_kernel_sem);
		return -EUSERS; /* DOSCOM_OPROCESS */
	}

	/* First Call */
	if (the_doscom_kernel.num_process == 0) {
		if (_target_lock_init() != DOSCOM_SUCCESS) {
			up(&the_doscom_kernel_sem);
			return -EUSERS; /* DOSCOM_OPROCESS */
		}
	}

	the_doscom_kernel.num_process++;

	/* Wait for complete non-response request(FIFOCH_R2M_NOFITY, etc) */
	ret = wait_complete_r2m_req(R2M_REQ_TIMEOUT);
	if( ret != DOSCOM_SUCCESS )  {
		up(&the_doscom_kernel_sem);
		return -ETIMEDOUT; /* DOSCOM_BUSY */
	}

	/* Send process open event to T_OS */
	send_r2m_req(CTL_R2M_REQ_PROCESSOPEN, 0);

	up(&the_doscom_kernel_sem);

	DEBUG(DBG_OPEN, "%s: end\n", __FUNCTION__);

	return 0;
}

/*
 * RELEASE
 */
static int
doscom_release(struct inode *inode, struct file *filp)
{
	pid_t tgid;
	int loop, ret;
	uint32_t fifoch_mask = 0;

	DEBUG(DBG_OPEN, "%s: begin\n", __FUNCTION__);

	down(&the_doscom_kernel_sem);

	DEBUG(DBG_RELEASE, "RELEASE called\n");

	/* search assigned fifo channel from tgid */
	tgid = current->tgid;
	for (loop = 0; loop < MAX_NUM_FIFOCHS; loop++){
		if(tgid == the_doscom_kernel.fifoch_tgid[loop]){
			fifoch_mask |= (1 << loop);
			the_doscom_kernel.fifoch_tgid[loop] = 0;
		}
	}

	DEBUG(DBG_RELEASE, "RELEASE fifoch_mask 0x%x\n", fifoch_mask);

	/* update fifoch_mask */
	the_doscom_kernel.fifoch_mask &= ~fifoch_mask;

	the_doscom_kernel.num_process--;

	/* release lock */
	if (the_doscom_kernel.lockd_tgid == current->tgid) {
		doscom_unlock();
	}

	/* Wait for complete non-response request(FIFOCH_R2M_NOFITY, etc) */
	ret = wait_complete_r2m_req(R2M_REQ_TIMEOUT);
	if( ret != DOSCOM_SUCCESS )  {
		up(&the_doscom_kernel_sem);
		return -ETIMEDOUT; /* DOSCOM_BUSY */
	}

	/* Send process exit event to T_OS */
	send_r2m_req(CTL_R2M_REQ_PROCESSEXIT, fifoch_mask);
	/*
	 * Wait polling
	 * wait_event_interruptible will be error in case of KILL signal
	 */
	wait_complete_r2m_req(-1);

	/* Final Call */
	if (the_doscom_kernel.num_process == 0) {
		if (_target_lock_deinit() != DOSCOM_SUCCESS) {
			up(&the_doscom_kernel_sem);
			return -EUSERS; /* DOSCOM_OPROCESS */
		}
	}

	up(&the_doscom_kernel_sem);

	DEBUG(DBG_OPEN, "%s: end\n", __FUNCTION__);

	return 0;
}

/*
 * MMAP
 */
static int
doscom_mmap(struct file *filp, struct vm_area_struct *vma)
{
	DEBUG(DBG_CALLS, "MMAP called\n");
	DEBUG(DBG_CALLS, "start%d , end%d\n", (int)vma->vm_start, (int)(vma->vm_end));

	if ((vma->vm_end - vma->vm_start) > SREGION_SIZE) {
		return -EINVAL; /* DOSCOM_ESREGION */
	}

#if __TARGET_ARCH_ARM == 6
	// note: pgprot_noncached is possible too
	vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
#endif /* __TARGET_ARCH_ARM == 6 */

#ifdef SREGION_NONCACHE
	// Disable Cache
	vma->vm_flags    |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#elif defined(SREGION_WRITECOMBINE)	/* SREGION_NONCACHE */
	//SYNC_WRITECOMBINE
	vma->vm_flags    |= VM_IO;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#endif /* SREGION_WRITECOMBINE */

	if (remap_pfn_range(vma,
						vma->vm_start,
						SREGION_PHYS_ADDRESS >> PAGE_SHIFT,
						vma->vm_end - vma->vm_start,
						vma->vm_page_prot)) {
		return -EAGAIN; /* DOSCOM_ESREGION */
	}

	return 0;
}

/*
 * IOCTLs
 */

static int
doscom_ioctl_init(doscom_init_data_t __user *user_init_data)
{
	doscom_init_data_t init_data;
	doscom_fifoch_event_t *p_fifoch_event;
	int loop, ret;

	DEBUG(DBG_OPEN, "%s: begin\n", __FUNCTION__);

	if (initialized) {
		DEBUG(DBG_INIT, "Doscom was already initialized\n");
		return 0;
	}

	DEBUG(DBG_INIT, "Get init data from user space\n");

	if (copy_from_user(&init_data, user_init_data, sizeof(init_data))) {
		DEBUG(DBG_OPEN, "%s: end #0\n", __FUNCTION__);
		return -EFAULT; /* DOSCOM_ESREGION */
	}

	DEBUG(DBG_INIT, "num_fifochs:%u, sregion_pending:0x%X \n",
					init_data.num_fifochs,
					init_data.sregion_pending_events);

	if ((init_data.num_fifochs > MAX_NUM_FIFOCHS) ||
		(init_data.sregion_pending_events > MAX_FIFOCH_PENDING_OFFSET)) {
		DEBUG(DBG_INIT, "Bad init_data parameters\n");
		return -EINVAL; /* DOSCOM_EPARAM */
	}

	the_doscom_kernel.num_fifochs = init_data.num_fifochs;
	if (init_data.num_fifochs > 0) {
		the_doscom_kernel.fifoch_events = kmalloc(
			init_data.num_fifochs*sizeof(doscom_fifoch_event_t), GFP_KERNEL);
		if (!the_doscom_kernel.fifoch_events) {
			DEBUG(DBG_OPEN, "%s: end #1\n", __FUNCTION__);
			return -ENOMEM; /* DOSCOM_ESREGION */
		}
		DEBUG(DBG_INIT, "Initialize channel events\n");
		for(loop=0; loop<init_data.num_fifochs; loop++) {
			p_fifoch_event = &the_doscom_kernel.fifoch_events[loop];
			p_fifoch_event->id = loop;
			p_fifoch_event->m2r_pending = (uint32_t *)(doscom_sregion + init_data.sregion_pending_events) + loop;
			DEBUG(DBG_INIT, "p_fifoch_event->m2r_pending: 0x%p (val:0x%X)\n",
				  p_fifoch_event->m2r_pending, *(p_fifoch_event->m2r_pending));
			p_fifoch_event->r2m_pending = p_fifoch_event->m2r_pending + init_data.num_fifochs;
			DEBUG(DBG_INIT, "p_fifoch_event->r2m_pending: 0x%p (val:0x%X)\n",
				  p_fifoch_event->r2m_pending, *(p_fifoch_event->r2m_pending));
			init_waitqueue_head(&p_fifoch_event->fifoch_wq);
		}
	}

	initialized = true;

	/* Wait for complete non-response request(FIFOCH_R2M_NOFITY, etc) */
	ret = wait_complete_r2m_req(R2M_REQ_TIMEOUT);
	if( ret != DOSCOM_SUCCESS )  {
		return -ETIMEDOUT; /* DOSCOM_BUSY */
	}

	/* Send initialize request to T_OS */
	send_r2m_req(CTL_R2M_REQ_INIT, 0);

	DEBUG(DBG_OPEN, "%s: end #2\n", __FUNCTION__);

	return 0;
}

static int
doscom_ioctl_regfifoch(uint32_t fifoch_mask)
{
	int loop, ret;
	uint32_t mask;

	CHECK_INITIALIZED();

	if (!fifoch_mask){
		return 0;
	}

	/* already used */
	if ((the_doscom_kernel.fifoch_mask & fifoch_mask) != 0) {
		DEBUG(DBG_REGFIFOCH, "Used channel is selected\n");
		return -EALREADY; /* DOSCOM_EASSIGN */
	}

	/* set mask value */
	the_doscom_kernel.fifoch_mask = the_doscom_kernel.fifoch_mask | fifoch_mask;

	/*
	 * set pid (theses are used at doscom_release )
	 */
	mask = fifoch_mask;
	for (loop = 0; fifoch_mask != 0; loop++){
		if (fifoch_mask & 0x1){
			the_doscom_kernel.fifoch_tgid[loop] = current->tgid;
			DEBUG(DBG_REGFIFOCH, "fifo channel %d's  PID = %d\n", loop, current->tgid);
		}
		fifoch_mask = fifoch_mask >> 1;
	}

	/* Wait for complete non-response request(FIFOCH_R2M_NOFITY, etc) */
	ret = wait_complete_r2m_req(R2M_REQ_TIMEOUT);
	if( ret != DOSCOM_SUCCESS )  {
		return -ETIMEDOUT; /* DOSCOM_BUSY */
	}

	/* Send register fifoch request to T_OS */
	send_r2m_req(CTL_R2M_REQ_REGFIFOCH, mask);

	return 0;
}

static int
doscom_ioctl_notify(uint32_t fifoch_id)
{
	doscom_fifoch_event_t *p_fifoch_event;

	CHECK_INITIALIZED();
	CHECK_FIFOCH_ID(fifoch_id);
	CHECK_OWN_FIFOCH(fifoch_id);

	if( get_r2m_req() == CTL_M2R_REQ_NONE )  {
		p_fifoch_event = &the_doscom_kernel.fifoch_events[fifoch_id];
		*(p_fifoch_event->r2m_pending) = FIFOCH_R2M_NOFITY;

		/* Caution : This value is channel id (not mask)*/
		send_r2m_req_noresp(CTL_R2M_REQ_NOTIFY, fifoch_id);
		return 0;
	}
	else  {
		/* Master hasn't been ready. */
		return -EBUSY;
	}
}

static int
doscom_ioctl_wait(uint32_t fifoch_id)
{
	doscom_fifoch_event_t *p_fifoch_event;

	CHECK_INITIALIZED();
	CHECK_FIFOCH_ID(fifoch_id);
	CHECK_OWN_FIFOCH(fifoch_id);

	p_fifoch_event = &the_doscom_kernel.fifoch_events[fifoch_id];

	p_fifoch_event->wercd = FIFOCH_M2R_NONE;
	up(&the_doscom_kernel_sem);
	wait_event_interruptible(p_fifoch_event->fifoch_wq,
							 (*(p_fifoch_event->m2r_pending) != FIFOCH_M2R_NONE) ||
							 (p_fifoch_event->wercd != FIFOCH_M2R_NONE));
	down(&the_doscom_kernel_sem);
	*(p_fifoch_event->m2r_pending) = FIFOCH_M2R_NONE;

	if (p_fifoch_event->wercd == FIFOCH_M2R_INIT) {
		return -ERESTART; /* DOSCOM_RESTART */
	}

	return 0;
}

static int
doscom_ioctl_get_fifoch_mask(uint32_t *fifoch_mask)
{
	CHECK_INITIALIZED();

	if( copy_to_user((void __user *)fifoch_mask, (const void *)&(the_doscom_kernel.fifoch_mask), 4) )  {
		return -EFAULT; /* DOSCOM_OTHER */
	}

	return 0;
}

static int
doscom_ioctl_fifoch_init(uint32_t fifoch_id)
{
	doscom_fifoch_event_t *p_fifoch_event;
	int		ret;

	CHECK_INITIALIZED();
	CHECK_FIFOCH_ID(fifoch_id);
	CHECK_OWN_FIFOCH(fifoch_id);

	p_fifoch_event = &the_doscom_kernel.fifoch_events[fifoch_id];
	wake_up_interruptible(&p_fifoch_event->fifoch_wq);

	/* Wait for complete non-response request(FIFOCH_R2M_NOFITY, etc) */
	ret = wait_complete_r2m_req(R2M_REQ_TIMEOUT);
	if( ret != DOSCOM_SUCCESS )  {
		return -ETIMEDOUT; /* DOSCOM_BUSY */
	}

	/* Send initialize fifoch request to T_OS */
	send_r2m_req(CTL_R2M_REQ_INITFIFOCH, fifoch_id);

	return 0;
}

static long
doscom_ioctl(struct file *file,
			unsigned int cmd,
			unsigned long arg)
{
	long ret;
	DEBUG(DBG_CALLS, "IOCTL called (cmd:%u, arg:%lu)\n", cmd, arg);

	down(&the_doscom_kernel_sem);
	switch(cmd) {
	  case DOSCOM_IOCTL_CMD_INIT:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_INIT(%u)\n", cmd);
		ret = doscom_ioctl_init((doscom_init_data_t *)arg);
		break;

	  case DOSCOM_IOCTL_CMD_NOTIFY:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_NOTIFY(%u)\n", cmd);
		ret = doscom_ioctl_notify((uint32_t)arg);
		break;

	  case DOSCOM_IOCTL_CMD_WAIT:
		DEBUG(DBG_CALLS,
			  "DOSCOM_IOCTL_CMD_WAIT(cmd:%u arg:%lu)\n", cmd, arg);
		ret = doscom_ioctl_wait((uint32_t)arg);
		break;

	  case DOSCOM_IOCTL_CMD_REGFIFOCH:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_REGFIFOCH(%u)\n", cmd);
		ret = doscom_ioctl_regfifoch((uint32_t)arg);
		break;

	  case DOSCOM_IOCTL_CMD_GETFIFOCHMASK:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_GETFIFOCHMASK(%u)\n", cmd);
		ret = doscom_ioctl_get_fifoch_mask((uint32_t *)arg);
		break;

	  case DOSCOM_IOCTL_CMD_INITFIFO:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_INITFIFO(%u)\n", cmd);
		ret = doscom_ioctl_fifoch_init((uint32_t)arg);
		break;

	  case DOSCOM_IOCTL_CMD_TRYLOCK:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_TRYLOCK(%u)\n", cmd);
		ret = doscom_trylock();
		break;

	  case DOSCOM_IOCTL_CMD_UNLOCK:
		DEBUG(DBG_CALLS, "DOSCOM_IOCTL_CMD_UNLOCK(%u)\n", cmd);
		ret = doscom_unlock();
		break;

	  default:
		DEBUG(DBG_CALLS, "IOCTL cmd (%u): not available\n", cmd);
		ret = -ENOTTY;
	}
	up(&the_doscom_kernel_sem);
	return ret;
}

static struct file_operations doscom_fops = {
	.owner	= THIS_MODULE,
	.open	= doscom_open,
	.mmap	= doscom_mmap,
	.unlocked_ioctl = doscom_ioctl,
	.release = doscom_release,
};

#ifndef ALLOC_DEVICE_MANUALLY
static struct miscdevice doscom_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "doscom",
	.fops = &doscom_fops,
};
#else /* !ALLOC_DEVICE_MANUALLY */
static dev_t dev;
#endif /* ALLOC_DEVICE_MANUALLY */

/*
 * IRQ HANDLER
 */
irqreturn_t
doscom_irq_handler(int irq, void *dev_id)
{
	int i;
	doscom_fifoch_event_t *p_fifoch_event;

	/* Clear Interrupt */
	_target_int_clear();

	DEBUG(DBG_IRQ, "DOSCOM IRQ handler\n");

	/* Check acknowledge */
	if (get_r2m_req() == CTL_M2R_REQ_ACK) {
		DEBUG(DBG_IRQ, "Receive Ack.\n");
		the_doscom_kernel.r2m_req_ack = true;
		wake_up_interruptible(&(the_doscom_kernel.r2m_req_ack_wq));
		set_r2m_req(CTL_M2R_REQ_NONE);
	}

	if (!initialized) {
		return IRQ_HANDLED;
	}

	for(i=0; i<the_doscom_kernel.num_fifochs; i++) {
		p_fifoch_event = &the_doscom_kernel.fifoch_events[i];
		if (*(p_fifoch_event->m2r_pending) == FIFOCH_M2R_NONE) continue;
		p_fifoch_event->wercd = *(p_fifoch_event->m2r_pending);
		if (*(p_fifoch_event->m2r_pending) == FIFOCH_M2R_INIT) {
			/* case of fifoch initialize, no queuing m2r request */
			*(p_fifoch_event->m2r_pending) = FIFOCH_M2R_NONE;
		}
		wake_up_interruptible(&p_fifoch_event->fifoch_wq);
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(doscom_irq_handler);


/*
 * MODULE INIT
 */
static int __init doscom_init(void)
{
	int err;
	int loop;

	DEBUG(DBG_INIT, "DOSCOM allocate shared memory\n");

#if __TARGET_ARCH_ARM == 6
	doscom_sregion = ioremap_nocache(SREGION_PHYS_ADDRESS, SREGION_SIZE);
#else /* __TARGET_ARCH_ARM == 7 */
#if defined(SREGION_NONCACHE) || defined(SREGION_WRITECOMBINE)
	doscom_sregion = ioremap_nocache(SREGION_PHYS_ADDRESS, SREGION_SIZE);
#else /* !SREGION_NONCACHE or !SREGION_WRITECOMBINE */
	doscom_sregion = ioremap_cache(SREGION_PHYS_ADDRESS, SREGION_SIZE);
#endif /* SREGION_NONCACHE or SREGION_WRITECOMBINE */
#endif /* __TARGET_ARCH_ARM == 6 */

	if( doscom_sregion == NULL ) {
		DEBUG(DBG_INIT, "Failed to map the Shared Memory\n");
		return -EAGAIN;
	}

	DEBUG(DBG_INIT, "Shared Memory: 0x%X mapped to 0x%p\n",
		  (uint32_t)SREGION_PHYS_ADDRESS, doscom_sregion);

	DEBUG(DBG_INIT, "Register DOSCOM char device\n");

#ifndef ALLOC_DEVICE_MANUALLY
	err = misc_register(&doscom_dev);
	if (err) goto init_error;
#else  /* !ALLOC_DEVICE_MANUALLY */
	DEBUG(DBG_INIT, "Allocating DOSCOM major/minor\n");
	err = alloc_chrdev_region(&dev, 0, 1,"doscom");
	major = MAJOR(dev);

	DEBUG(DBG_INIT, "mknod /dev/doscom c %d 0 (/proc/devices)\n", major);
	DEBUG(DBG_INIT, "chmod 0664 /dev/doscom\n");

	DEBUG(DBG_INIT, "Register DOSCOM char device\n");
	cdev_init(&doscom_cdev, &doscom_fops);
	doscom_cdev.owner = THIS_MODULE;
	err = cdev_add (&doscom_cdev, dev, 1);
	if (err) goto init_error;
#endif /* ALLOC_DEVICE_MANUALLY */

	_target_int_init();

	the_doscom_kernel.fifoch_mask = 0;
	the_doscom_kernel.num_process = 0;
	for (loop = 0; loop < MAX_NUM_FIFOCHS; loop++){
		the_doscom_kernel.fifoch_tgid[loop] = 0;
	}

	return 0;

init_error:
	DEBUG(DBG_INIT, "Error %d at initialization", err);
	return err;
}

/*
 * MODULE EXIT
 */
static void __exit doscom_exit(void)
{
	DEBUG(DBG_INIT, "Goodbye DOSCOM\n");

	_target_int_exit();

#ifndef ALLOC_DEVICE_MANUALLY
	misc_deregister(&doscom_dev);
#else /* !ALLOC_DEVICE_MANUALLY */
	cdev_del(&doscom_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
#endif /* ALLOC_DEVICE_MANUALLY */
}

module_init(doscom_init);
module_exit(doscom_exit);

MODULE_AUTHOR("Shinya Honda <honda@ertl.jp>");
MODULE_DESCRIPTION("DOSCOM driver");
MODULE_LICENSE("GPL");
