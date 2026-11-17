/*
 * File: linux-imx/security/topmon/topmon-lkrg-integrity-notifier.c
 * Overview: Track LKRG load/unload and compare reference/current hashes under a mutex.
 * Functions:
 *   topmon_lkrg_verify_target(): Verify the monitored LKRG text while holding the state lock.
 *   topmon_lkrg_module_event(): Update the monitored target on module state transitions.
 *   topmon_lkrg_notifier_init(): Register the module notifier.
 *   topmon_lkrg_notifier_exit(): Unregister the module notifier.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/string.h>

#include "common/topmon-lkrg-integrity.h"

static DEFINE_MUTEX(topmon_target_lock);
static struct topmon_lkrg_target topmon_target;

int topmon_lkrg_verify_target(void)
{
    /* Variables */
    u8 current_hash[TOPMON_HASH_SIZE];
    int result;

    /* Assignments */
    result = 0;
    memset(current_hash, 0, sizeof(current_hash));

    /* Logic */
    mutex_lock(&topmon_target_lock);
    if (!topmon_target.active)
        goto unlock;

    result = topmon_calculate_sha256(
        (const u8 *)topmon_target.text_start,
        topmon_target.text_size, current_hash);
    if (result)
        goto unlock;

    if (memcmp(topmon_target.reference_hash, current_hash,
               TOPMON_HASH_SIZE) != 0) {
        pr_emerg(TOPMON_LOG_PREFIX "LKRG integrity violation.\n");
        pr_emerg(TOPMON_LOG_PREFIX "REF-HASH: %*phN\n",
                 TOPMON_HASH_SIZE, topmon_target.reference_hash);
        pr_emerg(TOPMON_LOG_PREFIX "CUR-HASH: %*phN\n",
                 TOPMON_HASH_SIZE, current_hash);
        emergency_restart();
        result = -EKEYREJECTED;
    }

unlock:
    mutex_unlock(&topmon_target_lock);
    return result;
}

static int topmon_lkrg_module_event(struct notifier_block *notifier,
                                   unsigned long action, void *data)
{
    /* Variables */
    struct module *module;
    unsigned long text_start;
    size_t text_size;
    u8 initial_hash[TOPMON_HASH_SIZE];
    int result;

    /* Assignments */
    module = data;
    text_start = 0;
    text_size = 0;
    result = 0;
    memset(initial_hash, 0, sizeof(initial_hash));

    /* Logic */
    if (!module || strcmp(module->name, TOPMON_TARGET_MODULE_NAME))
        return NOTIFY_DONE;

    mutex_lock(&topmon_target_lock);
    if (action == MODULE_STATE_GOING) {
        memset(&topmon_target, 0, sizeof(topmon_target));
        pr_info(TOPMON_LOG_PREFIX "LKRG unloaded.\n");
        goto unlock;
    }

    if (action != TOPMON_CAPTURE_ACTION || topmon_target.active)
        goto unlock;

    text_start = (unsigned long)module->core_layout.base;
#ifdef CONFIG_TOPMON_LKRG_CAPTURE_COMING
    text_size = TOPMON_COMING_HASH_SIZE;
#else
    text_size = module->core_layout.text_size;
#endif
    result = topmon_calculate_sha256((const u8 *)text_start,
                                     text_size, initial_hash);
    if (result) {
        pr_err(TOPMON_LOG_PREFIX "LKRG initial hash failed: %d\n", result);
        goto unlock;
    }

    topmon_target.text_start = text_start;
    topmon_target.text_size = text_size;
    memcpy(topmon_target.reference_hash, initial_hash, TOPMON_HASH_SIZE);
    topmon_target.active = true;
    pr_info(TOPMON_LOG_PREFIX "LKRG reference hash captured (%s).\n",
            TOPMON_CAPTURE_NAME);
    pr_info(TOPMON_LOG_PREFIX "REF-HASH: %*phN\n",
            TOPMON_HASH_SIZE, topmon_target.reference_hash);

unlock:
    mutex_unlock(&topmon_target_lock);
    return NOTIFY_OK;
}

static struct notifier_block topmon_module_notifier = {
    .notifier_call = topmon_lkrg_module_event,
};

int topmon_lkrg_notifier_init(void)
{
    /* Logic */
    return register_module_notifier(&topmon_module_notifier);
}

void topmon_lkrg_notifier_exit(void)
{
    /* Logic */
    unregister_module_notifier(&topmon_module_notifier);
}

/*
 * End of File: linux-imx/security/topmon/topmon-lkrg-integrity-notifier.c
 */
