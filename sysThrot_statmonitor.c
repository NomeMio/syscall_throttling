#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "sysThrot_statmonitor.h"

struct syscall_entry {
    unsigned int syscall_id;
    struct syscall_stat stat;
    struct list_head list;
};

static LIST_HEAD(syscall_list_head);
static DEFINE_SPINLOCK(stats_lock);

unsigned long peak_blocked;
unsigned long mean_blocked;
atomic_t epoch_blocked;

static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
    struct list_head *pos_node;
    loff_t i = 0;

    spin_lock(&stats_lock);

    if (*pos == 0) {
        seq_printf(s, "%-8s %-10s %-10s %-10s %-16s %-10s %-16s\n",
                   "NR","TOTAL BLOCKED", "MEAN BLOCKED","MEAN DELAY(ns)", "PEAK_DELAY(ms)", "PID", "COMMAND");
        seq_printf(s, "--------------------------------------------------------------------------------\n");
    }

    list_for_each(pos_node, &syscall_list_head) {
        struct syscall_entry *entry = list_entry(pos_node, struct syscall_entry, list);
        if (i == *pos)
            return entry;
        i++;
    }

    if (i == *pos) {
        return (void *)1;
    }

    return NULL;
}

static void *my_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    struct list_head *pos_node;
    struct syscall_entry *entry;
    loff_t i = 0;

    (*pos)++;

    if (v == (void *)1)
        return NULL;

    list_for_each(pos_node, &syscall_list_head) {
        entry = list_entry(pos_node, struct syscall_entry, list);
        if (i == *pos)
            return entry;
        i++;
    }

    if (i == *pos) {
        return (void *)1;
    }

    return NULL;
}

static void my_seq_stop(struct seq_file *s, void *v)
{
    spin_unlock(&stats_lock);
}

static int my_seq_show(struct seq_file *s, void *v)
{
    if (v == (void *)1) {
        seq_printf(s, "\n%-14s %-14s\n", "PEAK_BLOCKED", "MEAN_BLOCKED");
        seq_printf(s, "----------------------------\n");
        seq_printf(s, "%-14lu %-14lu\n", peak_blocked, mean_blocked);
        return 0;
    }

    struct syscall_entry *entry = (struct syscall_entry *)v;
    
    seq_printf(s, "%-8u %-16lu %-10lu %-16lu\n",
               entry->syscall_id,
                entry->stat.total_blocked,
                entry->stat.mean_blocked,
                entry->stat.mean_delay_ns/1000000,
                entry->stat.peak_delay_ns / 1000000,
                entry->stat.peak_delay_pid,
                entry->stat.peak_delay_comm);
               
    return 0;
}

static const struct seq_operations my_seq_ops = {
    .start = my_seq_start,
    .next  = my_seq_next,
    .stop  = my_seq_stop,
    .show  = my_seq_show,
};

static int my_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &my_seq_ops);
}

static const struct proc_ops my_proc_ops = {
    .proc_open    = my_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = seq_release,
};

void sysThrot_add_blocked(unsigned int syscall_id, unsigned long delay_ns)
{
    struct list_head *pos_node;
    struct syscall_entry *entry = NULL;
    struct syscall_entry *new_entry = NULL;

    if (syscall_id >= SUPPORTED_SYSCALLS)
        return;

    atomic_inc(&epoch_blocked);

    spin_lock(&stats_lock);
    list_for_each(pos_node, &syscall_list_head) {
        struct syscall_entry *e = list_entry(pos_node, struct syscall_entry, list);
        if (e->syscall_id == syscall_id) {
            entry = e;
            break;
        }
    }

    if (!entry) {
        spin_unlock(&stats_lock);
        new_entry = kmalloc(sizeof(*new_entry), GFP_ATOMIC);
        if (!new_entry)
            return;
        atomic_set(&new_entry->stat.current_blocked, 0);
        new_entry->stat.total_blocked = 0;
        new_entry->stat.mean_blocked = 0;
        new_entry->stat.mean_delay_ns = 0;
        new_entry->stat.current_time_blocked = 0;
        new_entry->syscall_id = syscall_id;
        memset(&new_entry->stat, 0, sizeof(struct syscall_stat));
        INIT_LIST_HEAD(&new_entry->list);

        spin_lock(&stats_lock);
        list_for_each(pos_node, &syscall_list_head) {
            struct syscall_entry *e = list_entry(pos_node, struct syscall_entry, list);
            if (e->syscall_id == syscall_id) {
                entry = e;
                break;
            }
        }
        if (!entry) {
            list_add_tail(&new_entry->list, &syscall_list_head);
            entry = new_entry;
        } else {
            kfree(new_entry);
        }
    }
    atomic_inc(&entry->stat.current_blocked);
    entry->stat.current_time_blocked+=delay_ns;

    if (delay_ns > entry->stat.peak_delay_ns) {
        entry->stat.peak_delay_ns = delay_ns;
        entry->stat.peak_delay_pid = __kuid_val(current_uid());
        strscpy(entry->stat.peak_delay_comm, current->comm, TASK_COMM_LEN);
    }

    spin_unlock(&stats_lock);
}

void update_epoch_stats(void)
{
    //TODO: pessima syncro
    spin_lock(&stats_lock);
    unsigned long total_blocked = atomic_xchg(&epoch_blocked, 0);
    peak_blocked = max(peak_blocked, total_blocked);
    if (mean_blocked == 0) {
        mean_blocked = total_blocked;
    } else {
        mean_blocked = (mean_blocked * 29 + total_blocked) / 30;
    }
    struct list_head *entry_p = NULL;
    list_for_each( entry_p, &syscall_list_head) {
        struct syscall_entry *entry = list_entry(entry_p, struct syscall_entry, list);
        unsigned long current_blocked = atomic_xchg(&entry->stat.current_blocked, 0);
        entry->stat.total_blocked += current_blocked;
        if (current_blocked > 0) {
            entry->stat.mean_blocked = (entry->stat.mean_blocked * 29 + current_blocked) / 30;
            entry->stat.mean_delay_ns = (entry->stat.mean_delay_ns * 29 + entry->stat.current_time_blocked) / 30;
            entry->stat.current_time_blocked = 0;
        }
    }
    spin_unlock(&stats_lock);
}

int sysThrot_statmonitor_init(void)
{
    atomic_set(&epoch_blocked, 0);
    if (!proc_create(MODNAME, 0444, NULL, &my_proc_ops))
        return -ENOMEM;
    return 0;
}

void sysThrot_statmonitor_exit(void)
{
    struct list_head *pos_node, *q;
    struct syscall_entry *entry;

    remove_proc_entry(MODNAME, NULL);

    spin_lock(&stats_lock);
    list_for_each_safe(pos_node, q, &syscall_list_head) {
        entry = list_entry(pos_node, struct syscall_entry, list);
        list_del(pos_node);
        kfree(entry);
    }
    spin_unlock(&stats_lock);
}