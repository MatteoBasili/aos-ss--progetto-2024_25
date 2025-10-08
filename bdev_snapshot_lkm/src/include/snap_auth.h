#ifndef _SNAP_AUTH_H
#define _SNAP_AUTH_H

#define PW_HASH_LEN 32 /* SHA-256 output length in bytes */

/* Password entry, protected via RCU */
struct pw_entry {
    u8 hash[PW_HASH_LEN];
    struct rcu_head rh;
};

int bdev_auth_init(void);
void bdev_auth_exit(void);

int check_permission(void);
int set_snap_password(const char *pw, size_t pwlen);
bool verify_snap_password(const char *pw, size_t pwlen);

#endif

