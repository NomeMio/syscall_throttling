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

static unsigned long epoch_count;

static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
    struct list_head *pos_node;
    loff_t i = 0;

    spin_lock(&stats_lock);

    if (*pos == 0) {
        seq_printf(s, "%-8s %-16s %-17s %-16s %-16s %-16s %-16s %-10s %-10s %-16s\n",
                   "NR","TOTAL BLOCKED","BLOCKED/EPOCH","MEAN DELAY(ms)","PEAK_DELAY(ms)","MEAN CTX SWITCH","PEAK CTX SWITCH","PID","UID","PEAK COMMAND");
        seq_printf(s, "------------------------------------------------------------------------------------------------------------------------------------\n");
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
    struct list_head *pos_node;
    list_for_each(pos_node, &syscall_list_head) {
        struct syscall_entry *entry = list_entry(pos_node, struct syscall_entry, list);
        entry->stat.sum_ctx_switches = 0;
        entry->stat.peak_ctx_switches = 0;
    }
    epoch_count = 0;
    spin_unlock(&stats_lock);
}

static int my_seq_show(struct seq_file *s, void *v)
{
    if (v == (void *)1)
        return 0;

    struct syscall_entry *entry = (struct syscall_entry *)v;

    seq_printf(s, "%-8u %-16lu %-17lu %-16lu %-16lu %-16lu %-16lu %-10d %-10d %-16s\n",
               entry->syscall_id,
               entry->stat.total_blocked,
               entry->stat.last_epoch_blocked,
               entry->stat.last_epoch_mean_delay_ns / 1000000,
               entry->stat.peak_delay_ns / 1000000,
               epoch_count ? entry->stat.sum_ctx_switches / epoch_count : 0,
               entry->stat.peak_ctx_switches,
               entry->stat.peak_delay_pid,
               entry->stat.peak_delay_uid,
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

static struct syscall_entry *find_entry_locked(unsigned int syscall_id)
{
    struct list_head *pos_node;
    list_for_each(pos_node, &syscall_list_head) {
        struct syscall_entry *e = list_entry(pos_node, struct syscall_entry, list);
        if (e->syscall_id == syscall_id)
            return e;
    }
    return NULL;
}

static struct syscall_entry *find_or_create_entry_locked(unsigned int syscall_id)
{
    struct syscall_entry *entry = find_entry_locked(syscall_id);
    if (entry)
        return entry;

    struct syscall_entry *new_entry = kmalloc(sizeof(*new_entry), GFP_ATOMIC);
    if (!new_entry)
        return NULL;
    new_entry->syscall_id = syscall_id;
    memset(&new_entry->stat, 0, sizeof(struct syscall_stat));
    INIT_LIST_HEAD(&new_entry->list);
    list_add_tail(&new_entry->list, &syscall_list_head);
    return new_entry;
}

void sysThrot_add_blocked(unsigned int syscall_id, unsigned long delay_ns)
{
    if (syscall_id >= SUPPORTED_SYSCALLS)
        return;

    spin_lock(&stats_lock);
    struct syscall_entry *entry = find_or_create_entry_locked(syscall_id);
    if (!entry) {
        spin_unlock(&stats_lock);
        return;
    }
    atomic_inc(&entry->stat.current_blocked);
    entry->stat.current_time_blocked+=delay_ns;

    if (delay_ns > entry->stat.peak_delay_ns) {
        entry->stat.peak_delay_ns = delay_ns;
        entry->stat.peak_delay_pid = task_pid_nr(current);
        entry->stat.peak_delay_uid = __kuid_val(current_uid());
        strscpy(entry->stat.peak_delay_comm, current->comm, TASK_COMM_LEN);
    }

    spin_unlock(&stats_lock);
}

void sysThrot_add_ctx_switch(unsigned int syscall_id)
{
    if (syscall_id >= SUPPORTED_SYSCALLS)
        return;

    spin_lock(&stats_lock);
    struct syscall_entry *entry = find_or_create_entry_locked(syscall_id);
    if (entry)
        atomic_inc(&entry->stat.current_ctx_switches);
    spin_unlock(&stats_lock);
}

void update_epoch_stats(void)
{
    //TODO: pessima syncro
    spin_lock(&stats_lock);
    epoch_count++;
    struct list_head *entry_p = NULL;
    list_for_each( entry_p, &syscall_list_head) {
        struct syscall_entry *entry = list_entry(entry_p, struct syscall_entry, list);
        unsigned long current_blocked = atomic_xchg(&entry->stat.current_blocked, 0);
        unsigned long blocked_ns = entry->stat.current_time_blocked;
        entry->stat.current_time_blocked = 0;
        entry->stat.last_epoch_blocked = current_blocked;
        entry->stat.last_epoch_mean_delay_ns = current_blocked ? blocked_ns / current_blocked : 0;
        entry->stat.total_blocked += current_blocked;
        unsigned long ctx = atomic_xchg(&entry->stat.current_ctx_switches, 0);
        entry->stat.sum_ctx_switches += ctx;
        entry->stat.peak_ctx_switches = max(entry->stat.peak_ctx_switches, ctx);
    }
    spin_unlock(&stats_lock);
}

int sysThrot_statmonitor_init(void)
{
    epoch_count = 0;
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