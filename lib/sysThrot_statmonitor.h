#ifndef SYSTHROT_STATMONITOR_H
#define SYSTHROT_STATMONITOR_H

#include <linux/types.h>
#include <linux/sched.h>
#include "sysThrot.h"

struct syscall_stat {
    unsigned long peak_delay_ns;
    pid_t peak_delay_pid;
    char peak_delay_comm[TASK_COMM_LEN];
};

extern struct syscall_stat monitor_stats[SUPPORTED_SYSCALLS];



int sysThrot_statmonitor_init(void);
void sysThrot_statmonitor_exit(void);
void sysThrot_add_blocked(unsigned int syscall_id, unsigned long delay_ns);
void update_epoch_stats(void);


#endif