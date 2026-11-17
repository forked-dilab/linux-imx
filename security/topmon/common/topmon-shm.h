/*
 * File: linux-imx/security/topmon/common/topmon-shm.h
 * Overview: Define the unchanged IRQ 87 shared-memory ABI; use reference_hash naming.
 */

#ifndef TOPMON_SHM_H
#define TOPMON_SHM_H

#include <linux/types.h>

#define TOPMON_SHM_BASE_ADDRESS       0xBF000000U
#define TOPMON_INTEGRITY_SHM_OFFSET   0x00000280U
#define TOPMON_INTEGRITY_SHM_ADDRESS  (TOPMON_SHM_BASE_ADDRESS + TOPMON_INTEGRITY_SHM_OFFSET)
#define TOPMON_INTEGRITY_MAGIC        0x4C4B5247U
#define TOPMON_INTEGRITY_STATUS_ACTIVE 1U

/* Preserve the shared-memory field order and size for TOPPERS/FMP3. */
struct lkrg_mod_info {
    u32 magic;
    u32 status;
    u64 phys_addr;
    u64 size;
    u64 timestamp_counter;
    u8 reference_hash[32];
};

#endif /* TOPMON_SHM_H */

/*
 * End of File: linux-imx/security/topmon/common/topmon-shm.h
 */
