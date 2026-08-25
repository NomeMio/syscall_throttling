#ifndef SYSTHROT_STATMONITOR_H
#define SYSTHROT_STATMONITOR_H

#include <linux/types.h>
#include <linux/sched.h>
#include "sysThrot.h"

struct syscall_stat {
    atomic_t current_blocked;                /* blocked calls in the current (running) epoch */
    unsigned long current_time_blocked;      /* accumulated blocked ns in the current epoch */
    unsigned long total_blocked;             /* cumulative blocked calls */
    unsigned long last_epoch_blocked;        /* blocked calls in the last completed epoch */
    unsigned long last_epoch_mean_delay_ns;  /* mean blocked ns in the last completed epoch */
    atomic_t current_ctx_switches;           /* wait-condition evaluations in the current epoch */
    unsigned long sum_ctx_switches;          /* accumulated over epochs since last read */
    unsigned long peak_ctx_switches;         /* max per-epoch wait-condition evaluations */
    unsigned long peak_delay_ns;
    pid_t peak_delay_pid;
    uid_t peak_delay_uid;
    char peak_delay_comm[TASK_COMM_LEN];
};



int sysThrot_statmonitor_init(void);
void sysThrot_statmonitor_exit(void);
void sysThrot_add_blocked(unsigned int syscall_id, unsigned long delay_ns);
void sysThrot_add_ctx_switch(unsigned int syscall_id);
void update_epoch_stats(void);


#endif