#include <crypto/hash.h>
#include <linux/capability.h>

#include "bdev_auth.h"
#include "mod_name.h"

static struct crypto_shash *sha_tfm;
/* pw_current è un puntatore RCU a struct pw_entry che si assume contenga
 * un campo struct rcu_head rh; definizione di struct pw_entry attesa in
 * bdev_auth.h o header collegati.
 */
static struct pw_entry __rcu *pw_current;

/* mutex per serializzare set/clear della password.
 * Le operazioni di verifica rimangono lock-free (usano rcu_read_lock()).
 */
static DEFINE_MUTEX(pw_lock);

/* --- Helpers --- */
int check_permission(void)
{
    if (!capable(CAP_SYS_ADMIN)) {
        pr_warn("%s: operation denied, insufficient privileges\n", MOD_NAME);
        return -EPERM;
    }
    return 0;
}

static inline bool valid_pw(const char *pw, size_t pwlen)
{
    return pw && pwlen > 0 && pwlen <= SNAP_PASSWORD_MAX;
}

/* --- Calcola SHA-256 con alloc temporanea per shash_desc + contesto --- */
static int compute_sha256(const char *buf, size_t len, u8 *out)
{
    struct shash_desc *sdesc;
    int size, ret;

    if (!sha_tfm)
        return -EINVAL;

    /* size = sizeof(shash_desc) + spazio richiesto dal transform */
    size = sizeof(*sdesc) + crypto_shash_descsize(sha_tfm);

    sdesc = kmalloc(size, GFP_KERNEL);
    if (!sdesc)
        return -ENOMEM;

    /* Inizializza campi richiesti */
    sdesc->tfm = sha_tfm;

    ret = crypto_shash_init(sdesc);
    if (ret)
        goto out;

    ret = crypto_shash_update(sdesc, buf, len);
    if (ret)
        goto out;

    ret = crypto_shash_final(sdesc, out);

out:
    kfree(sdesc);
    return ret;
}

/* --- Set snapshot password --- */
int set_snap_password(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    struct pw_entry *new_pw, *old_pw;

    if (!valid_pw(pw, pwlen))
        return -EINVAL;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret) {
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return ret;
    }

    new_pw = kmalloc(sizeof(*new_pw), GFP_KERNEL);
    if (!new_pw) {
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return -ENOMEM;
    }

    /* copia hash e pulisci tmp */
    memcpy(new_pw->hash, tmp_hash, PW_HASH_LEN);
    memzero_explicit(tmp_hash, PW_HASH_LEN);

    /* Serializza update con mutex; aggiorna puntatore RCU */
    mutex_lock(&pw_lock);
    old_pw = rcu_dereference(pw_current);
    rcu_assign_pointer(pw_current, new_pw);
    mutex_unlock(&pw_lock);

    /* libera vecchia entry in modo sicuro (fuori dal lock) */
    if (old_pw)
        kfree_rcu(old_pw, rh);

    return 0;
}

/* --- Verify password lock-free con RCU --- */
bool verify_snap_password(const char *pw, size_t pwlen)
{
    int ret;
    u8 tmp_hash[PW_HASH_LEN];
    bool match = false;
    struct pw_entry *cur;

    if (!valid_pw(pw, pwlen))
        return false;

    ret = compute_sha256(pw, pwlen, tmp_hash);
    if (ret) {
        memzero_explicit(tmp_hash, PW_HASH_LEN);
        return false;
    }

    rcu_read_lock();
    cur = rcu_dereference(pw_current);
    if (cur) {
        u8 diff = 0;
        for (size_t i = 0; i < PW_HASH_LEN; i++) {
            diff |= cur->hash[i] ^ tmp_hash[i];
        }
        match = (diff == 0);
    }
    rcu_read_unlock();

    memzero_explicit(tmp_hash, PW_HASH_LEN);
    return match;
}

/* --- Clear password from memory --- */
static void clear_snap_password(void)
{
    struct pw_entry *old_pw;

    mutex_lock(&pw_lock);
    old_pw = rcu_dereference(pw_current);
    RCU_INIT_POINTER(pw_current, NULL);
    mutex_unlock(&pw_lock);

    if (old_pw)
        kfree_rcu(old_pw, rh);
}

/* --- Initialize crypto transform --- */
int bdev_auth_init(void)
{
    sha_tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(sha_tfm)) {
        pr_err("%s: crypto_alloc_shash failed\n", MOD_NAME);
        return PTR_ERR(sha_tfm);
    }

    return 0;
}

/* --- Cleanup --- */
void bdev_auth_exit(void)
{
    /* clear_snap_password() si occupa di rimuovere e kfree_rcu */
    clear_snap_password();

    if (sha_tfm) {
        crypto_free_shash(sha_tfm);
        sha_tfm = NULL;
    }
}

