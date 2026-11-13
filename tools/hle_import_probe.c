/* Query existing HLE registrations. A registered entry may still be a stub. */
#include "hle.h"
#include <stdio.h>
int main(void) {
    char dll[256], name[1024];
    while (scanf("%255s %1023s", dll, name) == 2)
        printf("%s\t%s\t%d\n", dll, name, !!hle_resolve_impl(dll, name));
    return 0;
}
