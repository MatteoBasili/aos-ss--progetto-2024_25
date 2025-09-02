#ifndef _BDEV_PASSWORD_H
#define _BDEV_PASSWORD_H

int password_init(void);
void password_exit(void);
bool is_password_set(void);

int set_password_from_user(const char *pw, size_t pwlen);
bool verify_password_from_user(const char *pw, size_t pwlen);

#endif

