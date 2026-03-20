#include <seccomp.h>
#include <stdio.h>

int main() {
    char *name = seccomp_syscall_resolve_num_arch(SCMP_ARCH_X86_64, 59);
    printf("Syscall 59 is: %s\n", name); // Outputs: execve
    return 0;
}