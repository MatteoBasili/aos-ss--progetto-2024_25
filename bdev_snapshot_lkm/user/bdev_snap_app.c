#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "uapi/bdev_snapshot.h"

#define PRINT_FOR_MORE_INFO_MSG \
    fprintf(stderr, "For more information, please check the kernel log.\n")

/* -------------------------------------------------------------------
 * Utility functions
 * ------------------------------------------------------------------- */
static void print_separator(void)
{
    printf("------------------------------------------------------------\n");
}

/* --- Securely zero out sensitive memory --- */
static void secure_memzero(void *v, size_t n)
{
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    explicit_bzero(v, n);
#else
    volatile unsigned char *p = v;
    while (n--) {
        *p++ = 0;
    }
#endif
}

/* --- Flush stdin --- */
static void flush_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
        ;
}

/* --- Read password --- */
static int read_password(char *buf, size_t buflen, const char *prompt)
{
    if (!buf || buflen == 0)
        return -1;

    struct termios oldt, newt;
    sigset_t sig_block, sig_old;

    buf[0] = '\0';

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: stdin is not a terminal\n");
        return -1;
    }

    sigemptyset(&sig_block);
    sigaddset(&sig_block, SIGINT);
    sigaddset(&sig_block, SIGTERM);
    sigaddset(&sig_block, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &sig_block, &sig_old) != 0) {
        perror("sigprocmask");
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        perror("tcgetattr");
        sigprocmask(SIG_SETMASK, &sig_old, NULL);
        return -1;
    }

    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // no echo, raw input

    if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) {
        perror("tcsetattr");
        sigprocmask(SIG_SETMASK, &sig_old, NULL);
        return -1;
    }

    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        size_t pos = 0;
        int c;
        while ((c = getchar()) != EOF) {
            if (c == '\n') {
                buf[pos] = '\0';
                break;
            } else if (c == 127 || c == '\b') { // backspace
                if (pos > 0) pos--;
            } else if (pos < buflen - 1 && isprint(c)) {
                buf[pos++] = (char)c;
            } else if (pos >= buflen - 1) {
                printf("\a"); // bell
                fflush(stdout);
            }
        }

        printf("\n");

        if (buf[0] == '\0') {
            printf("Password cannot be empty. Please try again.\n");
            continue;
        }
        break;
    }

    if (tcsetattr(STDIN_FILENO, TCSANOW, &oldt) != 0)
        fprintf(stderr, "Warning: failed to restore terminal state\n");
    sigprocmask(SIG_SETMASK, &sig_old, NULL);

    return 0;
}

/* --- Prompt for device name --- */
static int prompt_dev_name(char *dev, size_t size)
{
    while (1) {
        printf("Enter device name (or 'q' to cancel): ");
        fflush(stdout);

        struct termios oldt, newt;
        if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
            perror("tcgetattr");
            return -1;
        }
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // modalità raw, senza echo automatico
        if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) != 0) {
            perror("tcsetattr");
            return -1;
        }

        size_t pos = 0;
        int c;
        while ((c = getchar()) != EOF) {
            if (c == '\n') {
                dev[pos] = '\0';
                break;
            } else if (c == 127 || c == '\b') { // backspace
                if (pos > 0) {
                    pos--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (pos < size - 1 && isprint(c)) {
                dev[pos++] = (char)c;
                putchar(c);
                fflush(stdout);
            } else if (pos >= size - 1) {
                printf("\a"); // bell (beep)
                fflush(stdout);
            }
        }

        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        printf("\n");

        if (strcmp(dev, "q") == 0)
            return 1;
        if (dev[0] == '\0') {
            printf("Device name cannot be empty. Please try again.\n");
            continue;
        }

        return 0;
    }
}

/* --- Ask for a valid device name --- */
static int get_valid_dev_name(char *dev, size_t size)
{
    return prompt_dev_name(dev, size);
}

/* -------------------------------------------------------------------
 * IOCTL operations
 * ------------------------------------------------------------------- */

/* --- Activate or deactivate snapshot --- */
static void do_snap(int fd, unsigned long code)
{
    struct snap_args args;
    memset(&args, 0, sizeof(args));

    char dev[DEV_NAME_LEN_MAX];
    if (get_valid_dev_name(dev, sizeof(dev)) != 0)
        return;
    snprintf(args.dev_name, sizeof(args.dev_name), "%s", dev);

    if (read_password(args.password, sizeof(args.password),
                      "Enter snapshot password (or 'q' to cancel): ") < 0)
        return;

    if (strcmp(args.password, "q") == 0) {
        secure_memzero(args.password, sizeof(args.password));
        return;
    }

    errno = 0;
    int ret = ioctl(fd, code, &args);
    if (ret < 0) {
        fprintf(stderr, "Snapshot %s failed: %s\n",
                (code == SNAP_ACTIVATE) ? "activation" : "deactivation",
                strerror(errno));
                
        PRINT_FOR_MORE_INFO_MSG;        
    } else if (ret == 1) {
        printf("Snapshot already %s for this device\n",
               (code == SNAP_ACTIVATE) ? "active" : "deactivated");
    } else {        
        printf("Snapshot %s successfully.\n",
               (code == SNAP_ACTIVATE) ? "activated" : "deactivated");
    }

    secure_memzero(args.password, sizeof(args.password));
}

/* --- Set new password --- */
static void do_setpw(int fd)
{
    struct pw_arg pwarg;
    memset(&pwarg, 0, sizeof(pwarg));

    if (read_password(pwarg.password, sizeof(pwarg.password),
                      "Enter new snapshot password (or 'q' to cancel): ") < 0)
        return;

    if (strcmp(pwarg.password, "q") == 0) {
        secure_memzero(pwarg.password, sizeof(pwarg.password));
        return;
    }

    errno = 0;
    if (ioctl(fd, SNAP_SETPW, &pwarg) < 0) {
        fprintf(stderr, "Failed to set snapshot password: %s\n", strerror(errno));
        
        PRINT_FOR_MORE_INFO_MSG;
    } else {
        printf("Password set successfully.\n");
    }

    secure_memzero(pwarg.password, sizeof(pwarg.password));
}

/* --- List snapshots for device --- */
static int do_list_snapshots(int fd, const char *dev,
                             char timestamps[MAX_SNAPSHOTS][SNAP_TIMESTAMP_MAX], int *count)
{
    struct snap_list_args args;
    memset(&args, 0, sizeof(args));
    snprintf(args.dev_name, sizeof(args.dev_name), "%s", dev);

    if (ioctl(fd, SNAP_LIST, &args) < 0) {
        if (errno == ENOENT) {
            printf("No snapshots available for this device.\n");
        } else {
            fprintf(stderr, "Unable to list snapshots: %s\n", strerror(errno));
            
            PRINT_FOR_MORE_INFO_MSG;
        }
        *count = 0;
        return -1;
    }

    if (args.count <= 0) {
        printf("No snapshots available for this device.\n");
        *count = 0;
        return 0;
    }

    int n = args.count > MAX_SNAPSHOTS ? MAX_SNAPSHOTS : args.count;
    for (int i = 0; i < n; i++) {
        snprintf(timestamps[i], SNAP_TIMESTAMP_MAX, "%s", args.timestamps[i]);
    }

    *count = n;
    return 0;
}

/* --- Restore snapshot --- */
static void do_restore_snapshot(int fd)
{
    char dev[DEV_NAME_LEN_MAX];
    char password[SNAP_PASSWORD_MAX];
    char timestamps[MAX_SNAPSHOTS][SNAP_TIMESTAMP_MAX];
    int count = 0;

    if (get_valid_dev_name(dev, sizeof(dev)) != 0)
        return;

    if (do_list_snapshots(fd, dev, timestamps, &count) < 0 || count == 0)
        return;

    printf("\nAvailable snapshots:\n");
    for (int i = 0; i < count; i++)
        printf("[%d] %s\n", i + 1, timestamps[i]);

    int sel = 0;
    while (1) {
        char buf[32];
        printf("\nSelect snapshot to restore (1-%d, or 'q' to cancel): ", count);
        if (!fgets(buf, sizeof(buf), stdin)) {
            fprintf(stderr, "Input error while selecting snapshot.\n");
            return;
        }
        buf[strcspn(buf, "\n")] = 0;
        if (strcmp(buf, "q") == 0)
            return;
            
        char *end;
        long val = strtol(buf, &end, 10);
        if (end == buf || *end != '\0' || val < 1 || val > count) {
            printf("Invalid selection. Please enter a number between 1 and %d.\n", count);
            continue;
        }
        sel = (int)val;
        break;
    }

    /* --- WARNING --- */
    printf("\n⚠️  WARNING ⚠️\n");
    printf("It is recommended to perform a snapshot restore only on an unmounted device.\n");
    printf("If the filesystem on the device is mounted, changes may not be visible immediately,\n");
    printf("and there is a risk of data inconsistency or corruption.\n");
    printf("Proceeding with a restore on a mounted device may result in unexpected behavior.\n");
    printf("Make sure the device is completely unmounted before proceeding.\n\n");

    if (read_password(password, sizeof(password),
                      "Enter snapshot password (or 'q' to cancel): ") < 0)
        return;

    if (strcmp(password, "q") == 0) {
        secure_memzero(password, sizeof(password));
        return;
    }

    struct snap_restore_args args;
    memset(&args, 0, sizeof(args));
    snprintf(args.dev_name, sizeof(args.dev_name), "%s", dev);
    snprintf(args.timestamp, sizeof(args.timestamp), "%s", timestamps[sel - 1]);
    snprintf(args.password, sizeof(args.password), "%s", password);

    errno = 0;
    if (ioctl(fd, SNAP_RESTORE, &args) < 0) {
        if (errno == EBUSY) {
            fprintf(stderr, "Restore aborted: snapshot still in progress\n");
        } else {
            fprintf(stderr, "Restore failed: %s\n", strerror(errno));
            
            PRINT_FOR_MORE_INFO_MSG;
        }     
    } else {
        printf("Restore completed successfully.\n");
    }

    secure_memzero(password, sizeof(password));
}

/* -------------------------------------------------------------------
 * Menu
 * ------------------------------------------------------------------- */
enum menu_choice {
    MENU_ACTIVATE = 1,
    MENU_DEACTIVATE,
    MENU_RESTORE,
    MENU_SETPW,
    MENU_EXIT
};

static int menu(void)
{
    char buf[32];
    int choice = -1;

    print_separator();
    printf("\n\n=== Block-Device Snapshot Control Menu ===\n\n");
    printf("1) Activate snapshot\n");
    printf("2) Deactivate snapshot\n");
    printf("3) Restore snapshot\n");
    printf("4) Set password\n");
    printf("5) Exit\n\n");

    while (1) {
        printf("Select option (1-5): ");
        if (!fgets(buf, sizeof(buf), stdin)) {
            clearerr(stdin);
            continue;
        }

        if (!strchr(buf, '\n'))
            flush_stdin();

        buf[strcspn(buf, "\n")] = 0;

        char *end;
        long val = strtol(buf, &end, 10);
        if (end == buf || *end != '\0' || val < 1 || val > 5) {
            printf("Invalid input. Please enter a number between 1 and 5.\n\n");
            continue;
        }

        choice = (int)val;
        printf("You selected: %s\n\n",
               (choice == MENU_ACTIVATE) ? "Activate snapshot" :
               (choice == MENU_DEACTIVATE) ? "Deactivate snapshot" :
               (choice == MENU_RESTORE) ? "Restore snapshot" :
               (choice == MENU_SETPW) ? "Set password" : "Exit");
        break;
    }

    return choice;
}

/* -------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    if (argc > 1) {
        fprintf(stderr, "Usage: %s\n\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(SNAP_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n\n",
                SNAP_DEVICE_PATH, strerror(errno));
        return EXIT_FAILURE;
    }

    for (;;) {
        int choice = menu();

        switch (choice) {
            case MENU_ACTIVATE:
            case MENU_DEACTIVATE:
                do_snap(fd, (choice == MENU_ACTIVATE) ? SNAP_ACTIVATE : SNAP_DEACTIVATE);
                print_separator();
                break;

            case MENU_RESTORE:
                do_restore_snapshot(fd);
                print_separator();
                break;

            case MENU_SETPW:
                do_setpw(fd);
                print_separator();
                break;

            case MENU_EXIT:
                printf("Exiting...\n\n");
                close(fd);
                return EXIT_SUCCESS;
        }
    }
}

