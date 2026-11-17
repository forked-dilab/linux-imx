/*
 * File: linux-imx/security/topmon/common/topmon-timer.h
 * Overview: Read the physical ARM Generic Timer count and frequency.
 * Functions:
 *   topmon_timer_read_counter(): Read CNTPCT_EL0.
 *   topmon_timer_read_frequency_hz(): Read CNTFRQ_EL0.
 */

#ifndef TOPMON_TIMER_H
#define TOPMON_TIMER_H

#include <linux/types.h>
#include <asm/sysreg.h>

static inline u64 topmon_timer_read_counter(void)
{
    /* Logic */
    return read_sysreg(cntpct_el0);
}

static inline u32 topmon_timer_read_frequency_hz(void)
{
    /* Logic */
    return (u32)read_sysreg(cntfrq_el0);
}

#endif /* TOPMON_TIMER_H */

/*
 * End of File: linux-imx/security/topmon/common/topmon-timer.h
 */
