/* Test: Invalid-Free - Expected: 3 errors */
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int stack_var = 42;
    free(&stack_var);  // ERROR: stack variable
    
    free((void*)0xDEADBEEF);  // ERROR: random address
    
    void *ptr = malloc(100);
    free(ptr + 50);  // ERROR: offset pointer
    free(ptr);  // OK
    
    printf("Test: Invalid-Free\n");
    printf("Expected: 3 corruption errors\n");
    return 0;
}
