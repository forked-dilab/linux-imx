/*
 * DOSCOM Dual OS Communications Driver for Linux
 *
 * Copyright (C) Nagoya University
 * Daniel Sangorrin <daniel.sangorrin@gmail.com>
 * Shinya Honda     <honda@ertl.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  @(#) $Id: doscom_target_driver.h 82 2020-07-14 11:51:29Z ertl-honda $
 */

/*
 *  ARCHITECTURE
 */
#define __TARGET_ARCH_ARM 8

/*
 *  Configuration
 */
/* Use IPI driver */
#define USE_IPI_DRIVER

/*
 *  SHARED MEMORY (allocated in kernel parameters)
 */
#define SREGION_PHYS_ADDRESS    0xBF000000UL
#define SREGION_SIZE            0x00800000UL /* 8MB */
//#define SREGION_NONCACHE
#define SREGION_WRITECOMBINE

/*
 * IPI IRQ number
 *   Master IPI has interrupt handler. So Master IPI is defined at defivce tree.
 */
#define DOSCOM_R_IPI_INTNO		(43+32)

/*
 * Errors
 */
#define DOSCOM_SUCCESS         (0) /* function returned successfully */
#define DOSCOM_ERR_SEMAPHORE   (1) /* semaphore handling error */
#define DOSCOM_BUSY            (2) /* timeout */


#ifdef  DOSCOM_USE_LINUX_MUTEXDRIVER
#error "This option isn't supported."
#else  /* DOSCOM_USE_LINUX_MUTEXDRIVER */

static int
_target_lock_init(void)
{
	return DOSCOM_SUCCESS;
}

static int
_target_lock_deinit(void)
{
	return DOSCOM_SUCCESS;
}

/*
 *  _target_ioctl_trylock()
 *
 *  Function : This function try to spin lock.
 *  Argument : None
 *  Return :   0 -     Success
 *             Other - Failed
 */
static int
_target_lock_trylock(void)
{
	/* Never call */
	return 0;
}

/*
 *  _target_ioctl_tryunlock()
 *
 *  Function : This function try to spin unlock.
 *  Argument : None
 *  Return :   0 -     Success
 *             Other - Failed
 */
static int
_target_lock_unlock(void)
{
	/* Never call */
	return 0;
}
#endif  /* DOSCOM_USE_LINUX_MUTEXDRIVER */



/*
 * Prototypes in doscom-ipi.c
 */
extern void doscom_ipi_generate_int(void);

/*
 *  send_r2m_irq()
 *
 *  Function : This function occurs the interrupt to master core.
 *  Argument : None
 *  Return :   None
 */
static inline void
_target_int_send(void)
{
	doscom_ipi_generate_int();
}

static void
_target_int_init(void)
{
	/* do nothing */
}

static void
_target_int_exit(void)
{
	/* do nothing */
}

/*
 *  __doscom_kernel_clear_int()
 *
 *  Function : This function clears the ipi-interrupt event.
 *  Argument : None
 *  Return :   None
 */
static void
_target_int_clear(void)
{
	/* do nothing */
}
