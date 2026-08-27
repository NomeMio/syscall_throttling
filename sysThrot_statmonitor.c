#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "sysThrot_statmonitor.h"

#define HISTORY_LEN   20      /* number of epochs kept for the rolling window */

struct syscall_entry {
    unsigned int syscall_id;
    struct syscall_stat stat;
    unsigned long epoch_history[HISTORY_LEN];     /* blocked calls, one slot per epoch */
    unsigned long epoch_time_history[HISTORY_LEN];/* blocked ns, one slot per epoch */
    unsigned int history_head;                    /* next slot to write */
    unsigned int history_count;                   /* valid slots in the ring buffer */
    struct list_head list;
};

static LIST_HEAD(syscall_list_head);
static DEFINE_SPINLOCK(stats_lock);

static unsigned long epoch_count;

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
    memset(new_entry->epoch_history, 0, sizeof(new_entry->epoch_history));
    memset(new_entry->epoch_time_history, 0, sizeof(new_entry->epoch_time_history));
    new_entry->history_head = 0;
    new_entry->history_count = 0;
    INIT_LIST_HEAD(&new_entry->list);
    list_add_tail(&new_entry->list, &syscall_list_head);
    return new_entry;
}

/* value of the i-th oldest completed epoch (i in [0, history_count)) */
static unsigned long history_blocked_at(const struct syscall_entry *e, int i)
{
    return e->epoch_history[(e->history_head - e->history_count + i + HISTORY_LEN) % HISTORY_LEN];
}

static unsigned long history_time_at(const struct syscall_entry *e, int i)
{
    return e->epoch_time_history[(e->history_head - e->history_count + i + HISTORY_LEN) % HISTORY_LEN];
}

/* print ns as ms with 2 decimal places, without floating point */
static void format_ms(unsigned long ns, char *buf, size_t size)
{
    unsigned long m = ns / 10000; /* value in units of 0.01 ms */
    scnprintf(buf, size, "%lu.%02lu", m / 100, m % 100);
}

static void *my_seq_start(struct seq_file *s, loff_t *pos)
{
    struct list_head *pos_node;
    loff_t i = 0;

    spin_lock(&stats_lock);

    if (*pos == 0) {
        seq_printf(s, "=== Syscall Throttling Statistics ===\n");
        seq_printf(s, "  epochs elapsed: %lu   window: %d epochs   epoch duration: %d ms\n",
                   epoch_count, HISTORY_LEN, EPOCH_DURATION_MS);
        seq_printf(s, "  %4s %-22s %10s %8s %10s %9s %9s %6s %9s %9s  %s\n",
                   "NR", "SYSCALL", "TOTAL", "LAST", "AVG/EPOCH",
                   "MEAN(ms)", "PEAK(ms)", "ABORT", "CTX/EPOCH", "CTX PEAK",
                   "PEAK PROC (PID:UID)");
        seq_printf(s, "  -------------------------------------------------------------------------------------------------\n");
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
    if (v == (void *)1)
        return 0;

    struct syscall_entry *entry = (struct syscall_entry *)v;
    struct syscall_stat *st = &entry->stat;

    unsigned long window_sum = 0, window_time = 0;
    for (int i = 0; i < entry->history_count; i++) {
        window_sum += history_blocked_at(entry, i);
        window_time += history_time_at(entry, i);
    }
    unsigned long avg_epoch = entry->history_count ? window_sum / entry->history_count : 0;
    unsigned long avg_delay_ns = window_sum ? window_time / window_sum : 0;
    unsigned long avg_ctx = epoch_count ? st->sum_ctx_switches / epoch_count : 0;

    char mean_ms[16], peak_ms[16];
    format_ms(avg_delay_ns, mean_ms, sizeof(mean_ms));
    format_ms(st->peak_delay_ns, peak_ms, sizeof(peak_ms));

    seq_printf(s, "  %4u %-22s %10lu %8lu %10lu %9s %9s %6lu %9lu %9lu  %s (%d:%d)\n",
               entry->syscall_id,
               syscall_symbols[entry->syscall_id] ? syscall_symbols[entry->syscall_id] : "?",
               st->total_blocked,
               st->last_epoch_blocked,
               avg_epoch,
               mean_ms,
               peak_ms,
               st->total_aborted,
               avg_ctx,
               st->peak_ctx_switches,
               st->peak_delay_comm,
               st->peak_delay_pid,
               st->peak_delay_uid);

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

void sysThrot_add_aborted(unsigned int syscall_id)
{
    if (syscall_id >= SUPPORTED_SYSCALLS)
        return;

    spin_lock(&stats_lock);
    struct syscall_entry *entry = find_or_create_entry_locked(syscall_id);
    if (entry)
        entry->stat.total_aborted++;
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

        /* push the completed epoch into the per-syscall rolling window */
        entry->epoch_history[entry->history_head] = current_blocked;
        entry->epoch_time_history[entry->history_head] = blocked_ns;
        entry->history_head = (entry->history_head + 1) % HISTORY_LEN;
        if (entry->history_count < HISTORY_LEN)
            entry->history_count++;
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
