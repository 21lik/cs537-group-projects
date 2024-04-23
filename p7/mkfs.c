#include <stdio.h>

/**
 * Initialize a file to an empty filesystem.
*/
int main(int argc, char *argv[]) {

    char *d = NULL;
    int i = 0, b = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0)
            d = argv[++i];
        else if (strcmp(argv[i], "-i") == 0)
            i = atoi(argv[++i]);
        else if (strcmp(argv[i], "-b") == 0)
            b = atoi(argv[++i]);
    }

    // Round number of blocks up to nearest multiple of 32
    if (b % 32 != 0)
        b += 32 - (b % 32);
    
    // TODO: finish
}