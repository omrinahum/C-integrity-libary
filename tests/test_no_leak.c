/* Test: No Leaks - Expected: 0 leaks */
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    void *ptr1 = malloc(1024);
    void *ptr2 = malloc(512);
    void *ptr3 = malloc(256);
    
    free(ptr1);
    free(ptr2);
    free(ptr3);
    
    printf("Test: No Leaks\n");
    printf("Expected: 0 leaks\n");
    return 0;
}
