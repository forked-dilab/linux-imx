/*
 * File: linux-imx/security/topmon/topmon-lkrg-integrity.c
 * Overview: Coordinate TOPMON initialization, shutdown, and linker-defined code boundaries.
 * Functions:
 *   topmon_lkrg_init(): Initialize the enabled monitoring functions.
 *   topmon_lkrg_exit(): Release resources in reverse initialization order.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "common/topmon-lkrg-integrity.h"

extern char __topmon_lkrg_integrity_text_start[];
extern char __topmon_lkrg_integrity_text_end[];
EXPORT_SYMBOL_GPL(__topmon_lkrg_integrity_text_start);
EXPORT_SYMBOL_GPL(__topmon_lkrg_integrity_text_end);

static int __init topmon_lkrg_init(void)
{
    /* Variables */
    int result;

    /* Assignments */
    result = 0;

    /* Logic */
#ifdef CONFIG_TOPMON_LKRG_TEE
    result = topmon_lkrg_tee_init();
    if (result)
        return result;
#endif

#ifdef CONFIG_TOPMON_LKRG
    result = topmon_lkrg_notifier_init();
    if (result)
        goto error_tee;

    result = topmon_lkrg_check_init();
    if (result)
        goto error_notifier;
#endif

    pr_info(TOPMON_LOG_PREFIX "TOPMON initialized.\n");
    return 0;

#ifdef CONFIG_TOPMON_LKRG
error_notifier:
    topmon_lkrg_notifier_exit();
error_tee:
#ifdef CONFIG_TOPMON_LKRG_TEE
    topmon_lkrg_tee_exit();
#endif
    return result;
#endif
}

static void __exit topmon_lkrg_exit(void)
{
    /* Logic */
#ifdef CONFIG_TOPMON_LKRG
    topmon_lkrg_check_exit();
    topmon_lkrg_notifier_exit();
#endif
#ifdef CONFIG_TOPMON_LKRG_TEE
    topmon_lkrg_tee_exit();
#endif
    pr_info(TOPMON_LOG_PREFIX "TOPMON stopped.\n");
}

late_initcall(topmon_lkrg_init);
/* This code is built into the kernel; there is no module_exit registration. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TOPMON LKRG integrity monitor and TEE notifier");

/*
 * End of File: linux-imx/security/topmon/topmon-lkrg-integrity.c
 */
