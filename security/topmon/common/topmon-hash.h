/*
 * File: linux-imx/security/topmon/common/topmon-hash.h
 * Overview: Declare the common TOPMON SHA-256 calculation API.
 * Functions:
 *   topmon_calculate_sha256(): Calculate a SHA-256 digest.
 */

#ifndef TOPMON_HASH_H
#define TOPMON_HASH_H

#include <linux/types.h>

int topmon_calculate_sha256(const u8 *data, size_t size, u8 *hash);

#endif /* TOPMON_HASH_H */

/*
 * End of File: linux-imx/security/topmon/common/topmon-hash.h
 */
