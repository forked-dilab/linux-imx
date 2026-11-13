/*
 * File: vender_sources/linux-imx/security/integrity-zt/integrity-lkrg-shm.h
 * Overview: Shared memory and SMC definitions for integrity-lkrg module.
 */

#ifndef INTEGRITY_LKRG_SHM_H
#define INTEGRITY_LKRG_SHM_H

#include <linux/types.h>

#define DOSCOM_SHMEM_ADDRESS        0xBF000000U
#define INTEGRITY_LKRG_SHM_OFFSET   0x00000280U  /* Offset: 640 bytes */
#define INTEGRITY_LKRG_SHM_PHYS     (DOSCOM_SHMEM_ADDRESS + INTEGRITY_LKRG_SHM_OFFSET)

#define SMC_FUNC_ID_GEN_INT         0xbe000004U
#define IRQ_INTEGRITY_LKRG          87U          /* Non-colliding IRQ for TOPPERS */

#define LKRG_MOD_INFO_MAGIC         0x4C4B5247U  /* "LKRG" */

/**
 * Dynamic LKRG Module Info Structure (Linux -> TEE)
 * Mapped at Offset 0x280 (0xBF000280) from DOSCOM_SHMEM_ADDRESS.
 */
struct lkrg_mod_info {
    uint32_t magic;             /* Synchronization Magic: "LKRG" (0x4C4B5247) */
    uint32_t status;            /* 1: Active, 0: Disabled */
    uint64_t phys_addr;         /* Module text section physical base address */
    uint64_t size;              /* Text section size */
    uint64_t timestamp_cntpct;  /* Hardware physical counter (CNTPCT_EL0) at notification time */
    uint8_t  gold_hash[32];     /* SHA-256 reference hash calculated by Kernel */
};

/* Exported function prototype from doscom-ipi.c */
int doscom_send_lkrg_integrity_notify(uint64_t phys_addr, uint64_t size);

#endif /* INTEGRITY_LKRG_SHM_H */

/*
 * End of File: vender_sources/linux-imx/security/integrity-zt/integrity-lkrg-shm.h
 */