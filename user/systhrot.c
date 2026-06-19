#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "./lib/sysThrot.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <command> [args]\n"
        "\n"
        "Commands:\n"
        "  register-user    <uid>          Register a user UID\n"
        "  deregister-user  <uid>          Deregister a user UID\n"
        "  register-program <name>         Register a program by name\n"
        "  deregister-program <name>       Deregister a program by name\n"
        "  register-syscall <syscall>      Register a syscall (e.g. __x64_sys_getpid)\n"
        "  deregister-syscall <syscall>    Deregister a syscall\n"
        "  on                              Turn throttling on\n"
        "  off                             Turn throttling off\n",
        prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "register-user") == 0) {
        if (argc < 3) { fprintf(stderr, "register-user requires <uid>\n"); return EXIT_FAILURE; }
        return register_user(atoi(argv[2])) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "deregister-user") == 0) {
        if (argc < 3) { fprintf(stderr, "deregister-user requires <uid>\n"); return EXIT_FAILURE; }
        return deregister_user(atoi(argv[2])) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "register-program") == 0) {
        if (argc < 3) { fprintf(stderr, "register-program requires <name>\n"); return EXIT_FAILURE; }
        return register_program(argv[2]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "deregister-program") == 0) {
        if (argc < 3) { fprintf(stderr, "deregister-program requires <name>\n"); return EXIT_FAILURE; }
        return deregister_program(argv[2]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "register-syscall") == 0) {
        if (argc < 3) { fprintf(stderr, "register-syscall requires <syscall>\n"); return EXIT_FAILURE; }
        return register_syscall(argv[2]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "deregister-syscall") == 0) {
        if (argc < 3) { fprintf(stderr, "deregister-syscall requires <syscall>\n"); return EXIT_FAILURE; }
        return deregister_syscall(argv[2]) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "on") == 0) {
        return turn_on_monitor() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else if (strcmp(cmd, "off") == 0) {
        return turn_off_monitor() < 0 ? EXIT_FAILURE : EXIT_SUCCESS;

    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        usage(argv[0]);
        return EXIT_FAILURE;
    }
}
