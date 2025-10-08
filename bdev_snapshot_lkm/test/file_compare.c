#include <stdio.h>

int compare_files(const char *file1, const char *file2) {
    FILE *f1 = fopen(file1, "rb");
    if (!f1) {
        perror("Error opening file1");
        return -1;
    }

    FILE *f2 = fopen(file2, "rb");
    if (!f2) {
        perror("Error opening file2");
        fclose(f1);
        return -1;
    }

    int result = 0; // 0 = identical, 1 = different
    int ch1, ch2;

    do {
        ch1 = fgetc(f1);
        ch2 = fgetc(f2);

        if (ch1 != ch2) {
            result = 1;
            break;
        }
    } while (ch1 != EOF && ch2 != EOF);

    // Check if one file is longer than the other
    if ((ch1 != EOF) || (ch2 != EOF)) {
        result = 1;
    }

    fclose(f1);
    fclose(f2);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <file1> <file2>\n", argv[0]);
        return 1;
    }

    int cmp = compare_files(argv[1], argv[2]);
    if (cmp == 0) {
        printf("The files are identical.\n");
    } else if (cmp == 1) {
        printf("The files are different.\n");
    } else {
        printf("Error occurred during file comparison.\n");
    }

    return 0;
}

