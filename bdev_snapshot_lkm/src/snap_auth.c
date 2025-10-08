#include <crypto/hash.h>
#include <linux/capability.h>

#include "snap_auth.h"
#include "uapi/bdev_snapshot.h"

static struct crypto_shash *sha_tfm;
static struct pw_entry __rcu *pw_current;
static DEFINE_MUTEX(pw_lock);

int check_permission(void)
{
    if (!capable(CAP_SYS_ADMIN)) {
        pr_debug("%s: operation denied, insufficient privileges\n", MOD_NAME);
        return -EPERM;
    }
    return 0;
}

static void free_pw_entry_rcu(struct rcu_head *rh)
{
    struct pw_entry *pw = container_of(rh, struct pw_entry, rh);
    memzero_explicit(pw->hash, PW_HASH_LEN);
    kfree(pw);
}

/* Compute SHA-256 hash of input buffer */
static int compute_sha256(const char *buf, size_t len, u8 *out)
{
    int ret;
    struct shash_desc *sdesc;
    int size;

    if (!sha_tfm)
        return -EINVAL;

    /* Allocate space for descriptor + crypto context */
    size = sizeof(*sdesc) + crypto_shash_descsize(sha_tfm);
    sdesc = kzalloc(size, GFP_KERNEL);
    if (!sdesc)
        return -ENOMEM;

    sdesc->tfm = sha_tfm;

    ret = crypto_shash_init(sdesc);
    if (ret)
        goto out;

    ret = crypto_shash_update(sdesc, buf, len);
    if (ret)
        goto out;

    ret = crypto_shash_final(sdesc, out);

out:
    /* Clear sensitive data before freeing */
    memzero_explicit(sdesc, size);
    kfree(sdesc);
    return ret;
}

/* Set snapshot password (hash is stored with RCU protection) */
int set_snap_password(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    struct pw_entry *new_pw, *old_pw;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret) {
        pr_err("%s: failed to compute password hash (%d)\n", MOD_NAME, ret);
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return ret;
    }

    new_pw = kzalloc(sizeof(*new_pw), GFP_KERNEL);
    if (!new_pw) {
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return -ENOMEM;
    }

    memcpy(new_pw->hash, tmp_hash, PW_HASH_LEN);
    memzero_explicit(tmp_hash, PW_HASH_LEN);

    /* Serialize update with mutex; update pointer with RCU */
    mutex_lock(&pw_lock);
    old_pw = rcu_dereference_protected(pw_current,
                lockdep_is_held(&pw_lock));
    rcu_assign_pointer(pw_current, new_pw);
    mutex_unlock(&pw_lock);

    if (old_pw)
        call_rcu(&old_pw->rh, free_pw_entry_rcu);

    return 0;
}

/* Verify snapshot password */
bool verify_snap_password(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    bool match = false;
    struct pw_entry *cur;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret) {
        pr_err("%s: failed to compute password hash for verification (%d)\n",
               MOD_NAME, ret);
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return false;
    }

    rcu_read_lock();
    cur = rcu_dereference(pw_current);
    if (cur) {
        unsigned int diff = 0;
        for (size_t i = 0; i < PW_HASH_LEN; i++) {
            diff |= (unsigned int)(cur->hash[i] ^ tmp_hash[i]);
        }
        match = (diff == 0);
    }
    rcu_read_unlock();

    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return match;
}

/* Clear snapshot password safely */
static void clear_snap_password(void)
{
    struct pw_entry *old_pw;

    mutex_lock(&pw_lock);
    old_pw = rcu_dereference_protected(pw_current,
                lockdep_is_held(&pw_lock));
    rcu_assign_pointer(pw_current, NULL);
    mutex_unlock(&pw_lock);

    if (old_pw)
        call_rcu(&old_pw->rh, free_pw_entry_rcu);
}

int bdev_auth_init(void)
{
    sha_tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(sha_tfm)) {
        pr_err("%s: crypto_alloc_shash failed\n", MOD_NAME);
        return PTR_ERR(sha_tfm);
    }

    return 0;
}

void bdev_auth_exit(void)
{
    clear_snap_password();
    synchronize_rcu();

    if (sha_tfm) {
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
    }
}

