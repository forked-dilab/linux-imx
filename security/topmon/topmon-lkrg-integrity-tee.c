/*
 * File: linux-imx/security/topmon/topmon-lkrg-integrity-tee.c
 * Overview: Calculate TOPMON code reference hash and publish IRQ 87 shared-memory metadata.
 * Functions:
 *   topmon_lkrg_tee_init(): Map shared memory, calculate the reference hash, and notify TEE.
 *   topmon_lkrg_tee_exit(): Unmap shared memory.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/math64.h>
#include <linux/string.h>
#include <asm/memory.h>

#include "common/topmon-lkrg-integrity.h"
#include "common/topmon-shm.h"
#include "common/topmon-timer.h"
#include "../../drivers/topmon/common/topmon-smc.h"

extern char __topmon_lkrg_integrity_text_start[];
extern char __topmon_lkrg_integrity_text_end[];

static void __iomem *topmon_integrity_shared_memory;

int topmon_lkrg_tee_init(void)
{
    /* Variables */
    struct lkrg_mod_info info;
    u64 phys_addr;
    u64 text_size;
    u64 start_counter;
    u64 end_counter;
    u32 timer_frequency_hz;
    u64 elapsed_count;
    u64 elapsed_microseconds;
    u64 elapsed_microseconds_integer;
    u32 elapsed_microseconds_fraction;
    u32 frequency_mhz_integer;
    u32 frequency_mhz_fraction;
    int result;

    /* Assignments */
    phys_addr = (u64)__pa_symbol(__topmon_lkrg_integrity_text_start);
    text_size = (u64)(__topmon_lkrg_integrity_text_end - __topmon_lkrg_integrity_text_start);
    start_counter = 0;
    end_counter = 0;
    timer_frequency_hz = topmon_timer_read_frequency_hz();
    elapsed_count = 0;
    elapsed_microseconds = 0;
    elapsed_microseconds_integer = 0;
    elapsed_microseconds_fraction = 0;
    frequency_mhz_integer = 0;
    frequency_mhz_fraction = 0;
    result = 0;
    memset(&info, 0, sizeof(info));

    /* Logic */
    if (!text_size)
        return -EINVAL;

    if (!timer_frequency_hz)
        return -EINVAL;

    topmon_integrity_shared_memory = ioremap(TOPMON_INTEGRITY_SHM_ADDRESS,
                                             sizeof(info));
    if (!topmon_integrity_shared_memory)
        return -ENOMEM;

    result = topmon_calculate_sha256((const u8 *)__topmon_lkrg_integrity_text_start,
                                     (size_t)text_size, info.reference_hash);
    if (result)
        goto error_unmap;

    info.magic = TOPMON_INTEGRITY_MAGIC;
    info.status = TOPMON_INTEGRITY_STATUS_ACTIVE;
    info.phys_addr = phys_addr;
    info.size = text_size;
    start_counter = topmon_timer_read_counter();
    info.timestamp_counter = start_counter;

    memcpy_toio(topmon_integrity_shared_memory, &info, sizeof(info));
    wmb();
    result = smc_topmon_lkrg_integrity(phys_addr, text_size);
    end_counter = topmon_timer_read_counter();
    elapsed_count = end_counter - start_counter;
    /* The SMC interval is short enough to avoid overflow in this conversion. */
    elapsed_microseconds = div64_u64(elapsed_count * 100000000ULL,
                                      timer_frequency_hz);
    elapsed_microseconds_integer = div64_u64(elapsed_microseconds, 100U);
    elapsed_microseconds_fraction = (u32)(elapsed_microseconds % 100U);
    frequency_mhz_integer = timer_frequency_hz / 1000000U;
    frequency_mhz_fraction = (timer_frequency_hz % 1000000U) / 10000U;

    pr_info(TOPMON_LOG_PREFIX "TEE integrity notification: IRQ 87, result=%d\n", result);
    pr_info(TOPMON_LOG_PREFIX "REF-HASH: %*phN\n",
            TOPMON_HASH_SIZE, info.reference_hash);
    if (timer_frequency_hz % 1000000U == 0) {
        pr_info(TOPMON_LOG_PREFIX
                "SMC elapsed: %llu.%02u us(%llu count, %u MHz)\n",
                (unsigned long long)elapsed_microseconds_integer,
                elapsed_microseconds_fraction,
                (unsigned long long)elapsed_count,
                frequency_mhz_integer);
    } else {
        pr_info(TOPMON_LOG_PREFIX
                "SMC elapsed: %llu.%02u us(%llu count, %u.%02u MHz)\n",
                (unsigned long long)elapsed_microseconds_integer,
                elapsed_microseconds_fraction,
                (unsigned long long)elapsed_count,
                frequency_mhz_integer,
                frequency_mhz_fraction);
    }
    if (result)
        goto error_unmap;
    return 0;

error_unmap:
    iounmap(topmon_integrity_shared_memory);
    topmon_integrity_shared_memory = NULL;
    return result;
}

void topmon_lkrg_tee_exit(void)
{
    /* Logic */
    if (topmon_integrity_shared_memory) {
        iounmap(topmon_integrity_shared_memory);
        topmon_integrity_shared_memory = NULL;
    }
}

/*
 * End of File: linux-imx/security/topmon/topmon-lkrg-integrity-tee.c
 */
