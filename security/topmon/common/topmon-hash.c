/*
 * File: linux-imx/security/topmon/common/topmon-hash.c
 * Overview: Provide the common SHA-256 implementation for LKRG and TEE notification.
 * Functions:
 *   topmon_calculate_sha256(): Calculate a SHA-256 digest using the kernel Crypto API.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <crypto/hash.h>

#include "topmon-hash.h"

#define TOPMON_HASH_ALGORITHM "sha256"

int topmon_calculate_sha256(const u8 *data, size_t size, u8 *hash)
{
    /* Variables */
    struct crypto_shash *transform;
    struct shash_desc *descriptor;
    int result;

    /* Assignments */
    transform = NULL;
    descriptor = NULL;
    result = 0;

    /* Logic */
    if (!data || !hash || !size)
        return -EINVAL;

    transform = crypto_alloc_shash(TOPMON_HASH_ALGORITHM, 0, 0);
    if (IS_ERR(transform))
        return PTR_ERR(transform);

    descriptor = kmalloc(sizeof(*descriptor) + crypto_shash_descsize(transform),
                         GFP_KERNEL);
    if (!descriptor) {
        crypto_free_shash(transform);
        return -ENOMEM;
    }

    descriptor->tfm = transform;
    result = crypto_shash_digest(descriptor, data, size, hash);
    kfree(descriptor);
    crypto_free_shash(transform);
    return result;
}

/*
 * End of File: linux-imx/security/topmon/common/topmon-hash.c
 */
