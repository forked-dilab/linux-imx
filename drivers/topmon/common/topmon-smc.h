/*
 * File: linux-imx/drivers/topmon/common/topmon-smc.h
 * Overview: Declare TOPMON SMC APIs; expose TOPMON-specific exported function names.
 */

#ifndef TOPMON_SMC_H
#define TOPMON_SMC_H

#include <linux/types.h>

void smc_topmon_lkrg_alert(void);
int smc_topmon_lkrg_integrity(u64 phys_addr, u64 size);

#endif /* TOPMON_SMC_H */

/*
 * End of File: linux-imx/drivers/topmon/common/topmon-smc.h
 */
