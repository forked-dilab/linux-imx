/*
 * File: linux-imx/drivers/topmon/topmon-lkrg-alert.c
 * Overview: Notify TEE of LKRG alerts using the existing SMC and IRQ 85.
 * Functions:
 *   smc_topmon_lkrg_alert(): Issue an LKRG alert SMC notification.
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/barrier.h>

#include "common/topmon-config.h"
#include "common/topmon-smc.h"

void smc_topmon_lkrg_alert(void)
{
    /* Variables */
    struct arm_smccc_res result;
    u64 smc_function_id;
    u64 interrupt_id;

    /* Assignments */
    smc_function_id = TOPMON_SMC_FUNC_ID;
    interrupt_id = TOPMON_IRQ_LKRG_ALERT;

    /* Logic */
    /* Publish shared-memory writes before the Secure Monitor call. */
    dsb(sy);
    arm_smccc_smc(smc_function_id, interrupt_id, 0, 0,
                  0, 0, 0, 0, &result);
}
EXPORT_SYMBOL(smc_topmon_lkrg_alert);

/*
 * End of File: linux-imx/drivers/topmon/topmon-lkrg-alert.c
 */
