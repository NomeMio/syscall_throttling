#ifndef SYS_THROT_QUEUE_H
#define SYS_THROT_QUEUE_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include "syscalls.h"
#include "sysThrot.h"
struct _queue_elem {
    int *wake_up_flag;
    struct list_head node;
};

extern struct list_head global_queue;
extern spinlock_t queue_lock;
extern int epoche_max_elemesnts;
extern int current_in_queue;

int init_queue(void);
int add_to_queue(int *wake_up_flag);
int unqueue(int n);
int wake_up_queue(void);
int destroy_queue(void);

#endif