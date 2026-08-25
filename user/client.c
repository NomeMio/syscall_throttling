#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include "./lib/sysThrot.h"

#define DEFAULT_THREADS 16
#define DEFAULT_DURATION 5
#define MONITOR_PATH "/proc/SYSTHROT"

#define MAX_SYSCALLS 16
#define MAX_PROGRAMS 8

static const char *default_syscalls[] = {
	"__x64_sys_getpid", "__x64_sys_getuid", "__x64_sys_getppid",
	"__x64_sys_getgid", "__x64_sys_geteuid", "__x64_sys_gettid",
};

static const char *default_programs[] = {
	"st_load_gen1", "st_load_gen2", "st_load_gen3",
};

struct worker_arg {
	int syscall_nr;
	const char *comm;
	atomic_long ok;
	atomic_long eagain;
};

static atomic_int stop_flag;

static void *worker_job(void *arg) {
	struct worker_arg *a = (struct worker_arg *)arg;
	if (a->comm)
		prctl(PR_SET_NAME, a->comm, 0, 0, 0);
	while (!atomic_load(&stop_flag)) {
		long r = syscall(a->syscall_nr);
		if (r == -1 && errno == EAGAIN)
			atomic_fetch_add(&a->eagain, 1);
		else
			atomic_fetch_add(&a->ok, 1);
	}
	return NULL;
}

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int read_monitor(void) {
	int fd = open(MONITOR_PATH, O_RDONLY);
	if (fd < 0) {
		perror("open " MONITOR_PATH);
		return -1;
	}
	char buf[16384];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0) {
		perror("read " MONITOR_PATH);
		return -1;
	}
	buf[n] = '\0';
	printf("%s\n", buf);
	return 0;
}

static int is_already_done(void) {
	return errno == EEXIST || errno == EALREADY || errno == EINVAL;
}

static int setup_throttle(const char *const *progs, int nprogs,
                          const char *const *syscalls, int nsys) {
	if (register_user(getuid()) != 0 && errno != EEXIST) return -1;
	for (int i = 0; i < nprogs; i++)
		if (register_program(progs[i]) != 0 && errno != EEXIST) return -1;
	for (int i = 0; i < nsys; i++)
		if (register_syscall((char *)syscalls[i]) != 0 && errno != EEXIST) return -1;
	if (turn_on_monitor() != 0 && errno != EALREADY) return -1;
	return 0;
}

static int teardown_throttle(const char *const *progs, int nprogs,
                             const char *const *syscalls, int nsys) {
	if (turn_off_monitor() != 0 && errno != EALREADY) return -1;
	for (int i = 0; i < nsys; i++)
		if (deregister_syscall((char *)syscalls[i]) != 0 && errno != EEXIST) return -1;
	for (int i = 0; i < nprogs; i++)
		if (deregister_program(progs[i]) != 0 && !is_already_done()) return -1;
	if (deregister_user(getuid()) != 0 && !is_already_done()) return -1;
	return 0;
}

static int split_list(const char *list, const char *out[], int max) {
	if (!list || !*list)
		return 0;
	int n = 0;
	const char *p = list;
	while (*p && n < max) {
		const char *comma = strchr(p, ',');
		if (comma) {
			char *buf = strndup(p, comma - p);
			if (!buf) return -1;
			out[n++] = buf;
			p = comma + 1;
		} else {
			out[n++] = strdup(p);
			break;
		}
	}
	return n;
}

static void usage(const char *prog) {
	fprintf(stderr,
		"Usage: %s [-t NTHREADS] [-d SECONDS] [-s SYSCALLS] [-p PROGRAMS] [--setup] [--teardown]\n"
		"\n"
		"  -t NTHREADS   number of worker threads (default %d)\n"
		"  -d SECONDS    duration of the run (default %d)\n"
		"  -s SYSCALLS   comma-separated syscall symbol names to hammer\n"
		"                (default: %s)\n"
		"  -p PROGRAMS   comma-separated program names workers impersonate\n"
		"                (default: %s,...) names are limited to 15 chars\n"
		"  --setup       register user, programs and syscalls, then turn throttling on\n"
		"  --teardown    turn throttling off and deregister everything again\n",
		prog, DEFAULT_THREADS, DEFAULT_DURATION, default_syscalls[0], default_programs[0]);
}

int main(int argc, char **argv) {
	int threads = DEFAULT_THREADS;
	int duration = DEFAULT_DURATION;
	int do_setup = 0, do_teardown = 0;

	const char *syscall_list = NULL;
	const char *program_list = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			threads = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			duration = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			syscall_list = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			program_list = argv[++i];
		} else if (strcmp(argv[i], "--setup") == 0) {
			do_setup = 1;
		} else if (strcmp(argv[i], "--teardown") == 0) {
			do_teardown = 1;
		} else {
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (threads <= 0 || duration <= 0) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	const char *syscalls[MAX_SYSCALLS];
	const char *programs[MAX_PROGRAMS];
	int nsys = split_list(syscall_list, syscalls, MAX_SYSCALLS);
	int nprog = split_list(program_list, programs, MAX_PROGRAMS);
	if (nsys < 0 || nprog < 0) {
		fprintf(stderr, "failed to parse syscall/program list\n");
		return EXIT_FAILURE;
	}
	if (nsys == 0) {
		nsys = sizeof(default_syscalls) / sizeof(default_syscalls[0]);
		memcpy(syscalls, default_syscalls, sizeof(default_syscalls));
	}
	if (nprog == 0) {
		nprog = sizeof(default_programs) / sizeof(default_programs[0]);
		memcpy(programs, default_programs, sizeof(default_programs));
	}

	int nr[MAX_SYSCALLS];
	for (int i = 0; i < nsys; i++) {
		nr[i] = find_syscall_id(syscalls[i]);
		if (nr[i] < 0) {
			fprintf(stderr, "Syscall %s not found in supported syscalls\n", syscalls[i]);
			return EXIT_FAILURE;
		}
	}

	if (do_setup && setup_throttle(programs, nprog, syscalls, nsys) != 0)
		return EXIT_FAILURE;

	struct worker_arg *args = calloc(threads, sizeof(struct worker_arg));
	pthread_t *tids = calloc(threads, sizeof(pthread_t));
	if (!args || !tids) {
		perror("calloc");
		return EXIT_FAILURE;
	}

	atomic_store(&stop_flag, 0);
	for (int i = 0; i < threads; i++) {
		args[i].syscall_nr = nr[i % nsys];
		args[i].comm = programs[i % nprog];
		atomic_store(&args[i].ok, 0);
		atomic_store(&args[i].eagain, 0);
	}

	printf("PID: %d, hammering %d syscalls with %d threads (%d program names) for %d s\n",
	       getpid(), nsys, threads, nprog, duration);
	printf("Syscalls:");
	for (int i = 0; i < nsys; i++)
		printf(" %s", syscalls[i]);
	printf("\nPrograms:");
	for (int i = 0; i < nprog; i++)
		printf(" %s", programs[i]);
	printf("\n");

	for (int i = 0; i < threads; i++) {
		if (pthread_create(&tids[i], NULL, worker_job, &args[i]) != 0) {
			perror("pthread_create");
			atomic_store(&stop_flag, 1);
			for (int j = 0; j < i; j++)
				pthread_join(tids[j], NULL);
			return EXIT_FAILURE;
		}
	}

	long start = now_ms();
	while (now_ms() - start < duration * 1000L)
		usleep(10000);
	atomic_store(&stop_flag, 1);

	for (int i = 0; i < threads; i++)
		pthread_join(tids[i], NULL);

	long elapsed = now_ms() - start;
	long tot_ok = 0, tot_eg = 0;
	for (int i = 0; i < threads; i++) {
		tot_ok += atomic_load(&args[i].ok);
		tot_eg += atomic_load(&args[i].eagain);
	}
	printf("\n--- Results ---\n");
	printf("Elapsed:      %ld ms\n", elapsed);
	printf("OK calls:     %ld\n", tot_ok);
	printf("EAGAIN calls: %ld\n", tot_eg);
	if (elapsed > 0)
		printf("Throughput:   %.0f calls/s (%.0f throttled/s)\n",
		       (tot_ok + tot_eg) * 1000.0 / elapsed, tot_eg * 1000.0 / elapsed);

	printf("\nPer-syscall (OK / EAGAIN):\n");
	for (int i = 0; i < nsys; i++) {
		long ok = 0, eg = 0;
		for (int t = 0; t < threads; t++)
			if (args[t].syscall_nr == nr[i]) {
				ok += atomic_load(&args[t].ok);
				eg += atomic_load(&args[t].eagain);
			}
		printf("  %-22s %ld / %ld\n", syscalls[i], ok, eg);
	}

	printf("\n--- Monitor dump ---\n");
	read_monitor();

	free(tids);
	free(args);

	if (do_teardown && teardown_throttle(programs, nprog, syscalls, nsys) != 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
