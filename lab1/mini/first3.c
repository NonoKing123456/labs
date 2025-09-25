#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s string1 string2\n", argv[0]);
        return 1;
    }

    char *s1 = argv[1];
    char *s2 = argv[2];

    if (strlen(s1) < 3 || strlen(s2) < 3) {
        fprintf(stderr, "%s: error: one or more arguments have fewer than 3 characters\n", argv[0]);
        return 1;
    }

    // Print first 3 chars of each string
    for (int i = 0; i < 3; i++) {
        putchar(s1[i]);
    }
    for (int i = 0; i < 3; i++) {
        putchar(s2[i]);
    }
    putchar('\n');

    return 0;
}
