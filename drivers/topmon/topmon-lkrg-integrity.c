/*
 * File: linux-imx/drivers/topmon/topmon-lkrg-integrity.c
 * Overview: Notify TEE of TOPMON code integrity metadata using IRQ 87.
 * Functions:
 *   smc_topmon_lkrg_integrity(): Issue the TOPMON integrity SMC notification.
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/types.h>

#include "common/topmon-config.h"
#include "common/topmon-smc.h"

int smc_topmon_lkrg_integrity(u64 phys_addr, u64 size)
{
    /* Variables */
    struct arm_smccc_res result;
    u64 smc_function_id;
    u64 interrupt_id;

    /* Assignments */
    smc_function_id = TOPMON_SMC_FUNC_ID;
    interrupt_id = TOPMON_IRQ_LKRG_INTEGRITY;

    /* Logic */
    arm_smccc_smc(smc_function_id, interrupt_id, phys_addr, size,
                  0, 0, 0, 0, &result);

    return (int)result.a0;
}
EXPORT_SYMBOL_GPL(smc_topmon_lkrg_integrity);

/*
 * End of File: linux-imx/drivers/topmon/topmon-lkrg-integrity.c
 */
