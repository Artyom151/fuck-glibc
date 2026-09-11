#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gnu/libc-version.h>

int main(int argc, char **argv) {
    printf("\n============================================\n");
    printf("     GLIBC C Native Test on Alpine Linux    \n");
    printf("============================================\n");
    printf("[+] GNU libc runtime version: %s\n", gnu_get_libc_version());
    printf("[+] Process PID: %d, UID: %d, GID: %d\n", getpid(), getuid(), getgid());
    printf("[+] Arguments received:\n");
    for (int i = 0; i < argc; i++) {
        printf("    argv[%d] = %s\n", i, argv[i]);
    }
    printf("[+] Direct hardware access test:\n");
    if (access("/dev/dri/renderD128", R_OK | W_OK) == 0) {
        printf("    * /dev/dri/renderD128 is ACCESSIBLE (Hardware GPU Ready!)\n");
    } else {
        printf("    * /dev/dri/renderD128 is not directly accessible\n");
    }
    printf("[SUCCESS] C GLIBC execution test completed.\n\n");
    return 0;
}
