/* Test: Simple Leak - Expected: 2 leaks */
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    malloc(1024);
    malloc(512);
    
    void *ok = malloc(256);
    free(ok);
    
    printf("Test: Simple Leak\n");
    printf("Expected: 2 leaks (1024 + 512 bytes)\n");
    return 0;
}
