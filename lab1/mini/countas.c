#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <arg>\n", argv[0]);
        return 1;
    }

    char *s = argv[1];
    int count = 0;

    // Loop until we hit the null terminator '\0'
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == 'a') {
            count++;
        }
    }

    printf("%d\n", count);
    return 0;
}
