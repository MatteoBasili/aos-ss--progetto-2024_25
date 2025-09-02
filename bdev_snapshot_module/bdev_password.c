#include <linux/module.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "bdev_snapshot.h"
#include "bdev_password.h"

/* Stato password */
static u8 pw_hash[PW_HASH_LEN];
static bool pw_set = false;
static DEFINE_MUTEX(pw_mutex);
static struct crypto_shash *sha_tfm;

/* --- Compute SHA256 --- */
static int compute_sha256(const char *buf, size_t len, u8 *out)
{
    struct shash_desc *sdesc;
    int desc_size, ret;

    if (!sha_tfm)
        return -EINVAL;

    desc_size = sizeof(*sdesc) + crypto_shash_descsize(sha_tfm);
    sdesc = kmalloc(desc_size, GFP_KERNEL);
    if (!sdesc)
        return -ENOMEM;

    sdesc->tfm = sha_tfm;

    ret = crypto_shash_init(sdesc);
    if (ret) goto out;
    ret = crypto_shash_update(sdesc, buf, len);
    if (ret) goto out;
    ret = crypto_shash_final(sdesc, out);

out:
    kfree(sdesc);
    return ret;
}

int set_password_from_user(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];

    if (pwlen == 0 || pwlen > SNAP_PASSWORD_MAX)
        return -EINVAL;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret)
        return ret;

    mutex_lock(&pw_mutex);
    memcpy(pw_hash, tmp_hash, PW_HASH_LEN);
    pw_set = true;
    mutex_unlock(&pw_mutex);

    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return 0;
}

bool verify_password_from_user(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    bool match = false;

    if (!pw_set || pwlen == 0 || pwlen > SNAP_PASSWORD_MAX)
        return false;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret)
        return false;

    mutex_lock(&pw_mutex);
    match = (memcmp(pw_hash, tmp_hash, PW_HASH_LEN) == 0);
    mutex_unlock(&pw_mutex);

    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return match;
}

void clear_password(void)
{
    mutex_lock(&pw_mutex);
    if (pw_set)
        memzero_explicit(pw_hash, PW_HASH_LEN);
    pw_set = false;
    mutex_unlock(&pw_mutex);
}

bool is_password_set(void)
{
    return pw_set;
}

/* Alloc/free crypto transform */
int __init password_init(void)
{
    sha_tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(sha_tfm)) {
        pr_err("%s: crypto_alloc_shash failed\n", MOD_NAME);
        return PTR_ERR(sha_tfm);
    }
    return 0;
}

void password_exit(void)
{
    if (sha_tfm) {
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
    }
    clear_password();
}

