/*
 * File: linux-imx/security/topmon/common/topmon-lkrg-integrity.h
 * Overview: Declare internal LKRG monitoring APIs and the minimal shared state.
 */

#ifndef TOPMON_LKRG_INTEGRITY_H
#define TOPMON_LKRG_INTEGRITY_H

#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <crypto/sha.h>

#include "topmon-hash.h"

#define TOPMON_LOG_PREFIX "[TOPMON][REE] "
#define TOPMON_TARGET_MODULE_NAME "lkrg"
#define TOPMON_HASH_SIZE SHA256_DIGEST_SIZE
#define TOPMON_COMING_HASH_SIZE (2U * 4096U)
#define TOPMON_MILLISECONDS_PER_SECOND 1000U

#ifdef CONFIG_TOPMON_LKRG_CAPTURE_COMING
#define TOPMON_CAPTURE_ACTION MODULE_STATE_COMING
#define TOPMON_CAPTURE_NAME "COMING"
#else
#define TOPMON_CAPTURE_ACTION MODULE_STATE_LIVE
#define TOPMON_CAPTURE_NAME "LIVE"
#endif

struct topmon_lkrg_target {
    bool active;
    unsigned long text_start;
    size_t text_size;
    u8 reference_hash[TOPMON_HASH_SIZE];
};


#ifdef CONFIG_TOPMON_LKRG
int topmon_lkrg_notifier_init(void);
int topmon_lkrg_verify_target(void);
void topmon_lkrg_notifier_exit(void);
int topmon_lkrg_check_init(void);
void topmon_lkrg_check_exit(void);
#endif

#ifdef CONFIG_TOPMON_LKRG_TEE
int topmon_lkrg_tee_init(void);
void topmon_lkrg_tee_exit(void);
#endif

#endif /* TOPMON_LKRG_INTEGRITY_H */

/*
 * End of File: linux-imx/security/topmon/common/topmon-lkrg-integrity.h
 */
