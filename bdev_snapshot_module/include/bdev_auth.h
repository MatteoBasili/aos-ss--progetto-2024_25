#ifndef _BDEV_AUTH_H
#define _BDEV_AUTH_H

#define PW_HASH_LEN 32 /* SHA-256 */
#define SNAP_PASSWORD_MAX  64

/* --- Stato password protetto da RCU --- */
struct pw_entry {
    u8 hash[PW_HASH_LEN];
    struct rcu_head rh;   /* necessario per kfree_rcu */
};

int bdev_auth_init(void);
void bdev_auth_exit(void);

int check_permission(void);
int set_snap_password(const char *pw, size_t pwlen);
bool verify_snap_password(const char *pw, size_t pwlen);

#endif

