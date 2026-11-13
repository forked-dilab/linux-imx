/*
 * File: vender_sources/linux-imx/security/integrity-zt/integrity-lkrg.c
 * Overview: Kernel-side integrity module notifying Kernel text & calculated Gold Hash to TEE,
 *           and performing local periodic SHA256 integrity check for dynamic lkrg.ko.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/arch_timer.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <linux/reboot.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include "integrity-lkrg-shm.h"

#define SHA256_LENGTH 32
#define TARGET_MODULE_NAME "lkrg"

/* Delay TEE notification by 10 seconds to ensure boot & kernel self-modification are complete */
#define TEE_NOTIFY_DELAY_MS 10000

/* Convert Kconfig seconds to milliseconds */
#define CHECK_INTERVAL_MS (CONFIG_INTEGRITY_ZT_CHECK_INTERVAL_SEC * 1000)
#define CHECK_DATA_LENGTH_STATE_COMING (4096 * 2)
#define ZT_LOG_PREFIX "[DEBUG]: ZT-IoT "

/* Select trigger action based on Kconfig selection */
#ifdef CONFIG_INTEGRITY_ZT_CAPTURE_COMING
    #define TARGET_TRIGGER_ACTION MODULE_STATE_COMING
    #define TARGET_MODE_STR "COMING (Pre-init)"
    #define USE_FULL_TEXT_INTEGRITY false
#else
    #define TARGET_TRIGGER_ACTION MODULE_STATE_LIVE
    #define TARGET_MODE_STR "LIVE (Post-init)"
    #define USE_FULL_TEXT_INTEGRITY true
#endif

/* Symbols defining Kernel code boundaries */
// extern char _stext[], _etext[];
// extern char _sdata[];  /* Start of writable data section */
extern char __integrity_lkrg_text_start[];
extern char __integrity_lkrg_text_end[];
EXPORT_SYMBOL_GPL(__integrity_lkrg_text_start);
EXPORT_SYMBOL_GPL(__integrity_lkrg_text_end);

static struct timer_list lkrg_timer;
static void __iomem *shm_vaddr = NULL;

// static struct delayed_work tee_notify_dwork;
// static u64 saved_init_phys = 0;
// static u64 saved_init_size = 0;

struct lkrg_target_info {
    bool active;
    unsigned long text_start;    /* Virtual base address of lkrg.ko */
    unsigned long text_size;     /* Size of lkrg.ko text segment */
    u8 gold_hash[SHA256_LENGTH]; /* Initial SHA256 Hash value for lkrg.ko */
};

static struct lkrg_target_info target_data;

/* Helper function to calculate SHA256 hash using Kernel Crypto API */
static int calc_sha256(const u8 *data, size_t len, u8 *out_hash)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret;

    /* 1. Variables */
    tfm = NULL;
    desc = NULL;
    ret = 0;

    /* 2. Logic */
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) {
        pr_err(ZT_LOG_PREFIX "integrity-lkrg: Failed to allocate sha256 transform\n");
        return PTR_ERR(tfm);
    }

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        return -ENOMEM;
    }

    desc->tfm = tfm;

    ret = crypto_shash_digest(desc, data, len, out_hash);

    kfree(desc);
    crypto_free_shash(tfm);
    return ret;
}

/* Notify TEE via Shared Memory and doscom-ipi (Used ONLY for Kernel _stext region) */
static void notify_tee_kernel_integrity_info(u64 phys_addr, u64 size)
{
    struct lkrg_mod_info *shm;
    uint64_t current_cntpct;
    uint64_t smc_end_cntpct;
    u8 kernel_hash[SHA256_LENGTH];
    u64 size_kb_int;
    u64 size_kb_dec;
    int ret;

    /* 1. Variables */
    shm = NULL;
    current_cntpct = 0;
    smc_end_cntpct = 0;
    size_kb_int = 0;
    size_kb_dec = 0;
    ret = 0;
    memset(kernel_hash, 0, SHA256_LENGTH);

    /* 2. Assignments */
    if (!shm_vaddr) {
        pr_err(ZT_LOG_PREFIX "integrity-lkrg: SHM mapped address is NULL\n");
        return;
    }

    shm = (struct lkrg_mod_info *)shm_vaddr;

    /* 3. Logic */
    /* Calculate SHA-256 for Kernel text region (_stext ~ _etext) */
    /*
    if (calc_sha256((u8 *)_stext, (size_t)size, kernel_hash) != 0) {
        pr_err(ZT_LOG_PREFIX "integrity-lkrg: Failed to calculate Kernel text SHA256!\n");
        return;
    }
     */
    /* Calculate SHA256 only for the resident integrity-lkrg code */
    ret = calc_sha256(
        (const u8 *)__integrity_lkrg_text_start,
        (size_t)size,
        kernel_hash
    );

    if (ret != 0) {
        pr_err(ZT_LOG_PREFIX
            "integrity-lkrg: Failed to calculate integrity-lkrg SHA256!\n");
        return;
    }


    /* Populate Shared Memory Structure */
    shm->magic = LKRG_MOD_INFO_MAGIC;
    shm->status = 1;
    shm->phys_addr = phys_addr;
    shm->size = size;
    memcpy(shm->gold_hash, kernel_hash, SHA256_LENGTH);

    /* Publish the notification timestamp after the hash has been prepared. */
    current_cntpct = arch_timer_read_counter();
    shm->timestamp_cntpct = current_cntpct;

    /* Commit memory writes before issuing the notification. */
    wmb();

    /* Measure the existing IRQ 87 notification call without changing it. */
    ret = doscom_send_lkrg_integrity_notify(phys_addr, size);
    smc_end_cntpct = arch_timer_read_counter();

    pr_info(ZT_LOG_PREFIX
        "integrity-lkrg: [IRQ87][PERF] SMC_CALL: %llu counts (%llu ns), Ret=%d\n",
        (unsigned long long)(smc_end_cntpct - current_cntpct),
        (unsigned long long)((smc_end_cntpct - current_cntpct) * 125ULL), ret);

    /* Translate size to KB (Integer and 2 decimal places) */
    size_kb_int = size / 1024;
    size_kb_dec = ((size % 1024) * 100) / 1024;

    /* Formatted log outputs across separate lines for readability */
    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Notified TEE via doscom-ipi (%s mode). Phys = 0x%llx, Size = 0x%llx (%llu.%02llu KB / %llu bytes), REE_CNPCT: %llu, Ret = %d\n",
        TARGET_MODE_STR, phys_addr, size, size_kb_int, size_kb_dec, size, current_cntpct, ret);
    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Kernel Gold Hash = %*phN\n",
        SHA256_LENGTH, shm->gold_hash);
}

/* Module Notifier Callback Handler for Dynamic LKRG Detection */
static int lkrg_module_notify_handler(struct notifier_block *nb, unsigned long action, void *data)
{
    struct module *mod;

    /* 1. Variables */
    mod = (struct module *)data;

    /* 2. Logic */
    if (!mod || strcmp(mod->name, TARGET_MODULE_NAME) != 0) {
        return NOTIFY_OK;
    }

    /* Check if the module lifecycle action matches our target state (COMING or LIVE) */
    if (action == TARGET_TRIGGER_ACTION) {
        if (target_data.active) {
            return NOTIFY_OK;
        }

        target_data.text_start = (unsigned long)mod->core_layout.base;

        if (USE_FULL_TEXT_INTEGRITY) {
            target_data.text_size = (unsigned long)mod->core_layout.text_size;
        } else {
            target_data.text_size = CHECK_DATA_LENGTH_STATE_COMING;
        }

        /* Calculate initial Gold Hash directly using Virtual Address */
        if (calc_sha256((u8 *)target_data.text_start, target_data.text_size, target_data.gold_hash) == 0) {
            target_data.active = true;
            pr_info(ZT_LOG_PREFIX "integrity-lkrg: Dynamic LKRG module detected [%s]. Base = 0x%lx, Size = 0x%lx (Gold Hash calculated)\n",
                TARGET_MODE_STR, target_data.text_start, target_data.text_size);
        } else {
            pr_err(ZT_LOG_PREFIX "integrity-lkrg: Failed to calculate initial SHA256 for LKRG module.\n");
        }

    } else if (action == MODULE_STATE_GOING) {
        pr_info(ZT_LOG_PREFIX "integrity-lkrg: LKRG module unloading.\n");
        target_data.active = false;
        memset(target_data.gold_hash, 0, SHA256_LENGTH);
    }

    return NOTIFY_OK;
}

static struct notifier_block lkrg_nb = {
    .notifier_call = lkrg_module_notify_handler,
};

/* Periodic Heartbeat Timer Callback for LKRG module */
static void lkrg_integrity_periodic_check(struct timer_list *t)
{
    u8 current_hash[SHA256_LENGTH];
    int hash_ret;

    /* 1. Variables */
    hash_ret = -1;
    memset(current_hash, 0, SHA256_LENGTH);

    /* 2. Logic */
    if (target_data.active) {
        /* Re-calculate current hash using Virtual Address */
        hash_ret = calc_sha256((u8 *)target_data.text_start, target_data.text_size, current_hash);

        if (hash_ret == 0) {
            if (memcmp(target_data.gold_hash, current_hash, SHA256_LENGTH) != 0) {
                /* Log Mismatch Details before emergency restart */
                pr_emerg(ZT_LOG_PREFIX "CRITICAL: LKRG Module Hash Mismatch Detected!\n");
                pr_emerg(ZT_LOG_PREFIX "  Gold Hash : %*phN\n", SHA256_LENGTH, target_data.gold_hash);
                pr_emerg(ZT_LOG_PREFIX "  Calc Hash : %*phN\n", SHA256_LENGTH, current_hash);

                emergency_restart();
            } else {
                pr_debug(ZT_LOG_PREFIX "integrity-lkrg: Periodic check passed (LKRG hash valid).\n");
                pr_debug(ZT_LOG_PREFIX "  Gold Hash : %*phN\n", SHA256_LENGTH, target_data.gold_hash);
                pr_debug(ZT_LOG_PREFIX "  Calc Hash : %*phN\n", SHA256_LENGTH, current_hash);
            }
        } else {
            pr_err(ZT_LOG_PREFIX "integrity-lkrg: Failed to calculate current SHA256 in periodic check.\n");
        }
    }

    /* Re-arm periodic timer */
    mod_timer(&lkrg_timer, jiffies + msecs_to_jiffies(CHECK_INTERVAL_MS));
}

static int __init integrity_lkrg_init(void)
{
    u64 init_phys;
    u64 init_size;

    /* 1. Variables */
    init_phys = 0;
    init_size = 0;

    /* 2. Assignments */
    target_data.active = false;


    /* Calculate physical address and size of Kernel _stext region */
    init_phys = (u64)__pa_symbol(__integrity_lkrg_text_start);
    // init_phys = (u64)__pa_symbol(_stext);
    // init_phys = (u64)__pa_symbol(_stext);
    // init_size = (u64)((unsigned long)_etext - (unsigned long)_stext);

    /*
     * Use _sdata (Start of Data section) as the boundary to strictly isolate
     * executable code and read-only segments from writable data.
     */
    /*
    if ((unsigned long)_sdata > (unsigned long)_stext &&
        (unsigned long)_sdata < (unsigned long)_etext) {
        init_size = (u64)((unsigned long)_sdata - (unsigned long)_stext);
    } else {
        init_size = (u64)((unsigned long)_etext - (unsigned long)_stext);
    }
     */
    init_size = (u64)(__integrity_lkrg_text_end - __integrity_lkrg_text_start);

    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Initializing module (Target Mode: %s)...\n", TARGET_MODE_STR);
    // pr_info(ZT_LOG_PREFIX "integrity-lkrg: Using late_initcall_sync...\n");
    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Target Phys = 0x%llx, Pure Text Size = 0x%llx\n", init_phys, init_size);

    /* Map Shared Memory Region (Offset 0x280) */
    shm_vaddr = ioremap(INTEGRITY_LKRG_SHM_PHYS, sizeof(struct lkrg_mod_info));
    if (!shm_vaddr) {
        pr_err(ZT_LOG_PREFIX "integrity-lkrg: ioremap failed for 0x%x\n", INTEGRITY_LKRG_SHM_PHYS);
        return -ENOMEM;
    }

    /* 3. Logic */
    /* Issue initial SMC notification with calculated Gold Hash to TEE for Kernel _stext */
    notify_tee_kernel_integrity_info(init_phys, init_size);
    // notify_tee_kernel_integrity_info(init_phys, init_size);

    /* Register Kernel Module Notifier for dynamic lkrg.ko loading */
    register_module_notifier(&lkrg_nb);

    /* Setup Periodic Heartbeat Timer */
    timer_setup(&lkrg_timer, lkrg_integrity_periodic_check, 0);
    mod_timer(&lkrg_timer, jiffies + msecs_to_jiffies(CHECK_INTERVAL_MS));

    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Initialized successfully.\n");
    return 0;
}

static void __exit integrity_lkrg_exit(void)
{
    del_timer_sync(&lkrg_timer);
    unregister_module_notifier(&lkrg_nb);

    if (shm_vaddr) {
        iounmap(shm_vaddr);
        shm_vaddr = NULL;
    }

    pr_info(ZT_LOG_PREFIX "integrity-lkrg: Module exited.\n");
}

late_initcall(integrity_lkrg_init);
// late_initcall_sync(integrity_lkrg_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Integrity-LKRG Kernel Text TEE Notifier & LKRG Module Local Check");

/*
 * End of File: vender_sources/linux-imx/security/integrity-zt/integrity-lkrg.c
 */