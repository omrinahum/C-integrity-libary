/* Test: Double-Free - Expected: 2 errors */
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    void *ptr1 = malloc(100);
    free(ptr1);
    free(ptr1);  // ERROR: double-free
    
    void *ptr2 = malloc(200);
    free(ptr2);
    free(ptr2);  // ERROR: double-free
    
    printf("Test: Double-Free\n");
    printf("Expected: 2 corruption errors\n");
    return 0;
}
