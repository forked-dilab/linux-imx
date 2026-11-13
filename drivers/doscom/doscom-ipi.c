/*
 * DOSCOM Dual OS Communications Inter-processor interrupt Driver for i.MX8MM
 *
 * Copyright (C) Nagoya University
 *   Daniel Sangorrin <daniel.sangorrin@gmail.com>
 *   Shinya Honda     <honda@ertl.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "doscom_target_driver.h"

/* Bit definitions inside the function id as per the SMC calling convention */
#define FUNCID_TYPE_SHIFT   31
#define FUNCID_CC_SHIFT     30
#define FUNCID_OEN_SHIFT    24
#define FUNCID_NUM_SHIFT    0

#define FUNCID_TYPE_MASK    0x1
#define FUNCID_CC_MASK      0x1
#define FUNCID_OEN_MASK     0x3f
#define FUNCID_NUM_MASK     0xffff

#define FUNCID_TYPE_WIDTH   1
#define FUNCID_CC_WIDTH     1
#define FUNCID_OEN_WIDTH    6
#define FUNCID_NUM_WIDTH    16

#define GET_SMC_CC(id)      ((id >> FUNCID_CC_SHIFT) & \
                     FUNCID_CC_MASK)
#define GET_SMC_TYPE(id)    ((id >> FUNCID_TYPE_SHIFT) & \
                     FUNCID_TYPE_MASK)

#define SMC_64              1
#define SMC_32              0U
#define SMC_UNK             0xffffffff
#define SMC_TYPE_FAST       1U
#define SMC_TYPE_STD        0U
#define SMC_PREEMPTED       0xfffffffe

/* SMC Function call IDs */
#define ATFSMC_RV(func_num) \
        ((SMC_TYPE_FAST << FUNCID_TYPE_SHIFT) | \
         ((SMC_32) << FUNCID_CC_SHIFT) | \
         (62 << FUNCID_OEN_SHIFT) | \
         ((func_num) & FUNCID_NUM_MASK))

/* SETVCT: Set TOPPERS kernel entry points (fiq vector etc.) */
#define ATFSMC_SET_VECTOR           ATFSMC_RV(0)
/* INIT:   Notification TOPPERS/FMP initialize complete */
#define ATFSMC_INIT_COMPLETE        ATFSMC_RV(1)
/* CPUON:  Issued when returning from "cpu_on" vector */
#define ATFSMC_SMC_CPU_ON           ATFSMC_RV(2)
/* SWITCH: initiates a switch to the opposite world */
#define ATFSMC_SWITCH_SECURE_STATE  ATFSMC_RV(3)
/* GENINT: Generate secure interrupt */
#define ATFSMC_GEN_INT				ATFSMC_RV(4)

/* SMC Function call return codes */
#define	ATFSMC_RET_SUCCESS			0
#define	ATFSMC_RET_INVALID_STATE	-1
#define	ATFSMC_RET_NOT_READY		-2


/*
 *  Global variables
 */
static int                  r_ipi_hwirq = -1;
static int					m_ipi_irq;
static int                  m_ipi_hwirq = -1;


/*
 *  Prototypes
 */
extern irqreturn_t  doscom_irq_handler(int irq, void *dev_id);
static const struct of_device_id gic_match[];


/*
 *  doscom_ipi_probe()
 *
 *  Function : This function initilizes this driver.
 *  Argument : pdev - platform device structer
 *  Return :   0 -     Success
 *             Other - Failed
 */
static int  doscom_ipi_probe(struct platform_device *pdev)
{
	struct irq_data 	*irq_data;
    int 				ret;

    /*
     *  Get resoruce information from device tree
     */
    /* Get DOSCOM-master IPI system IRQ  from device tree */
    m_ipi_irq = platform_get_irq(pdev, 0);
    if( m_ipi_irq < 0 )
    {
        dev_err(&pdev->dev, "DOSCOM: DOSCOM-master IPI device node isn't found.\n");
        return -ENODEV;
    }

	irq_data = irq_get_irq_data(m_ipi_irq);
    if( irq_data > 0 )  {
		m_ipi_hwirq = irq_data->hwirq;
	}
	else  {
        dev_err(&pdev->dev, "DOSCOM: Failed to get DOSCOM-master IPI GIC IRQ.\n");
        return -ENODEV;
    }

    pr_info("DOSCOM: DOSCOM-master IPI GIC-IRQ : %d\n", m_ipi_hwirq);

    /* Get DOSCOM-remote IPI system IRQ  from device tree */
	r_ipi_hwirq = DOSCOM_R_IPI_INTNO;

    pr_info("DOSCOM: DOSCOM-remote IPI GIC-IRQ : %d\n", r_ipi_hwirq);

    /*
     *  Register DOSCOM-master IPI IRQ
     */
    /* Register DOSCOM IRQ handler */
    ret = devm_request_irq(&pdev->dev, m_ipi_irq,
        doscom_irq_handler, IRQF_SHARED, dev_name(&pdev->dev),
        &pdev->dev);
    if( ret )
    {
        dev_err(&pdev->dev, "IRQ %d already allocated\n", m_ipi_irq);
        return ret;
    }

    pr_info("DOSCOM: DOSCOM IRQ handler is registered.\n");
    pr_info("DOSCOM: DOSCOM GIC IPI driver initialized DOSCOM-remote IPI.\n");

    return 0;
}


/*
 *  doscom_ipi_remove()
 *
 *  Function : This function removes this driver.
 *  Argument : pdev - platform device structer
 *  Return :   0 -     Success
 *             Other - Failed
 */
static int  doscom_ipi_remove(struct platform_device *pdev)
{
    devm_free_irq(&pdev->dev, m_ipi_irq, &pdev->dev);
    m_ipi_irq = -1;

    return 0;
}


/*
 *  doscom_ipi_generate_int()
 *
 *  Function : This function generates interupt to the master processor.
 *  Argument : None
 *  Return :   None
 */
void        doscom_ipi_generate_int(void)
{
	__asm__ volatile("mov	x1, %0"::"r"((uint64_t)r_ipi_hwirq));
	__asm__ volatile("mov	x0, %0"::"r"(ATFSMC_GEN_INT));
	__asm__ volatile("dsb sy":::"memory");
	__asm__ volatile("smc 	#0");
}
EXPORT_SYMBOL(doscom_ipi_generate_int);


/*
 *  Tables
 */
static const struct of_device_id doscom_ipi_match[] = {
    { .compatible = "doscom-gic-ipi", },
    { /* end of list */ },
};
MODULE_DEVICE_TABLE(of, doscom_ipi_match);

static struct platform_driver doscom_ipi_driver = {
    .probe =  doscom_ipi_probe,
    .remove = doscom_ipi_remove,
    .driver = {
        .name = "doscom_ipi",
        .of_match_table = doscom_ipi_match,
    },
};
module_platform_driver(doscom_ipi_driver);

static const struct of_device_id gic_match[] = {
    { .compatible = "arm,gic-v3", },
    { /* end of list */ },
};

MODULE_DESCRIPTION("DOSCOM IPI driver");
MODULE_LICENSE("GPL v2");
