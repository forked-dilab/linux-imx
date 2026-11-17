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
#include <linux/math64.h>
#include <asm/sysreg.h>
#include <linux/workqueue.h>

#include "common/topmon-lkrg-integrity.h"

static struct delayed_work topmon_check_work;
static u64 topmon_check_sequence;

static void topmon_lkrg_check_work(struct work_struct *work)
{
    /* Variables */
    unsigned long interval_jiffies;
    u64 start_counter;
    u64 end_counter;
    u64 elapsed_counts;
    u64 elapsed_us_x100;
    u64 sequence;
    u32 timer_frequency_hz;
    int result;
    bool scheduled;

    /* Assignments */
    interval_jiffies = msecs_to_jiffies(
        CONFIG_TOPMON_LKRG_CHECK_INTERVAL_SEC * TOPMON_MILLISECONDS_PER_SECOND);
    timer_frequency_hz = (u32)read_sysreg(cntfrq_el0);
    start_counter = 0;
    end_counter = 0;
    elapsed_counts = 0;
    elapsed_us_x100 = 0;
    sequence = ++topmon_check_sequence;
    result = 0;
    scheduled = false;

    /* Logic */
    start_counter = (u64)read_sysreg(cntpct_el0);
    result = topmon_lkrg_verify_target();
    end_counter = (u64)read_sysreg(cntpct_el0);
    elapsed_counts = end_counter - start_counter;

    if (timer_frequency_hz) {
        elapsed_us_x100 = mul_u64_u32_div(elapsed_counts,
                                            100000000U,
                                            timer_frequency_hz);
        pr_info(TOPMON_LOG_PREFIX
                "[LKRG][CHECK] #%llu: Start=%llu, End=%llu, "
                "Elapsed=%llu counts (%llu.%02llu us, timer=%u Hz)\n",
                (unsigned long long)sequence,
                (unsigned long long)start_counter,
                (unsigned long long)end_counter,
                (unsigned long long)elapsed_counts,
                (unsigned long long)(elapsed_us_x100 / 100),
                (unsigned long long)(elapsed_us_x100 % 100),
                timer_frequency_hz);
    } else {
        pr_err(TOPMON_LOG_PREFIX
               "[LKRG][CHECK] #%llu: Invalid timer frequency; "
               "elapsed=%llu counts\n",
               (unsigned long long)sequence,
               (unsigned long long)elapsed_counts);
    }

    if (result)
        pr_err(TOPMON_LOG_PREFIX
               "[LKRG][CHECK] #%llu: Verification failed: %d\n",
               (unsigned long long)sequence, result);

    scheduled = schedule_delayed_work(&topmon_check_work, interval_jiffies);
    if (!scheduled)
        pr_err(TOPMON_LOG_PREFIX
               "[LKRG][CHECK] #%llu: Failed to schedule next check\n",
               (unsigned long long)sequence);
}

int topmon_lkrg_check_init(void)
{
    /* Variables */
    unsigned long interval_jiffies;
    bool scheduled;

    /* Assignments */
    interval_jiffies = msecs_to_jiffies(
        CONFIG_TOPMON_LKRG_CHECK_INTERVAL_SEC * TOPMON_MILLISECONDS_PER_SECOND);

    scheduled = false;
    topmon_check_sequence = 0;

    /* Logic */
    INIT_DELAYED_WORK(&topmon_check_work, topmon_lkrg_check_work);
    scheduled = schedule_delayed_work(&topmon_check_work, interval_jiffies);
    if (!scheduled) {
        pr_err(TOPMON_LOG_PREFIX "[LKRG][CHECK] Initial scheduling failed\n");
        return -EBUSY;
    }

    pr_info(TOPMON_LOG_PREFIX
            "[LKRG][CHECK] Periodic monitoring scheduled (interval=%u s)\n",
            CONFIG_TOPMON_LKRG_CHECK_INTERVAL_SEC);
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
