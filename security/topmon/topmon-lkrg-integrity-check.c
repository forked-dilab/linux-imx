/*
 * File: linux-imx/security/topmon/topmon-lkrg-integrity-check.c
 * Overview: Calculate SHA-256 and periodically verify the LKRG module in process context.
 * Functions:
 *   topmon_lkrg_check_work(): Run one integrity check and schedule the next check.
 *   topmon_lkrg_check_init(): Start periodic delayed work.
 *   topmon_lkrg_check_exit(): Stop periodic delayed work synchronously.
 */

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>

#include "common/topmon-lkrg-integrity.h"

static struct delayed_work topmon_check_work;

static void topmon_lkrg_check_work(struct work_struct *work)
{
    /* Variables */
    unsigned long interval_jiffies;
    int result;

    /* Assignments */
    interval_jiffies = msecs_to_jiffies(
        CONFIG_TOPMON_LKRG_CHECK_INTERVAL_SEC * TOPMON_MILLISECONDS_PER_SECOND);
    result = 0;

    /* Logic */
    result = topmon_lkrg_verify_target();
    if (result)
        pr_err(TOPMON_LOG_PREFIX "LKRG integrity check failed: %d\n", result);

    schedule_delayed_work(&topmon_check_work, interval_jiffies);
}

int topmon_lkrg_check_init(void)
{
    /* Variables */
    unsigned long interval_jiffies;

    /* Assignments */
    interval_jiffies = msecs_to_jiffies(
        CONFIG_TOPMON_LKRG_CHECK_INTERVAL_SEC * TOPMON_MILLISECONDS_PER_SECOND);

    /* Logic */
    INIT_DELAYED_WORK(&topmon_check_work, topmon_lkrg_check_work);
    schedule_delayed_work(&topmon_check_work, interval_jiffies);
    return 0;
}

void topmon_lkrg_check_exit(void)
{
    /* Logic */
    cancel_delayed_work_sync(&topmon_check_work);
}

/*
 * End of File: linux-imx/security/topmon/topmon-lkrg-integrity-check.c
 */
