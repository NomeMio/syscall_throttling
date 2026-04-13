#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stdarg.h>
#include <linux/string.h>
#include <linux/version.h>
#include "./sysThrot.h"

#define SYS_THR_LOG_NAME "sysThrot_log"
#define SYS_THR_LOG_BUFFER_SIZE (64 * 1024)
#define SYS_THR_LOG_LINE_SIZE 256

static char *sysThrot_log_buffer;
static size_t sysThrot_log_head;
static size_t sysThrot_log_len;
static DEFINE_MUTEX(sysThrot_log_lock);
static struct proc_dir_entry *sysThrot_log_proc_entry;

static void sysThrot_log_append_locked(const char *msg, size_t msg_len)
{
    size_t idx;

    if (!sysThrot_log_buffer || msg_len == 0)
        return;

    if (msg_len >= SYS_THR_LOG_BUFFER_SIZE) {
        msg += (msg_len - (SYS_THR_LOG_BUFFER_SIZE - 1));
        msg_len = SYS_THR_LOG_BUFFER_SIZE - 1;
    }

    idx = 0;
    while (idx < msg_len) {
        size_t pos = (sysThrot_log_head + sysThrot_log_len) % SYS_THR_LOG_BUFFER_SIZE;
        size_t writable = SYS_THR_LOG_BUFFER_SIZE - pos;
        size_t chunk = min(writable, msg_len - idx);

        memcpy(sysThrot_log_buffer + pos, msg + idx, chunk);
        idx += chunk;

        if (sysThrot_log_len < SYS_THR_LOG_BUFFER_SIZE)
            sysThrot_log_len += chunk;
        else
            sysThrot_log_head = (sysThrot_log_head + chunk) % SYS_THR_LOG_BUFFER_SIZE;

        if (sysThrot_log_len > SYS_THR_LOG_BUFFER_SIZE)
            sysThrot_log_len = SYS_THR_LOG_BUFFER_SIZE;
    }
}

void sysThrot_log(const char *fmt, ...)
{
    va_list args;
    char line[SYS_THR_LOG_LINE_SIZE];
    int written;

    if (!fmt)
        return;

    va_start(args, fmt);
    written = vscnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (written <= 0)
        return;

    if (written >= (int)sizeof(line) - 1)
        line[sizeof(line) - 2] = '\n';
    else if (line[written - 1] != '\n')
        line[written++] = '\n';

    mutex_lock(&sysThrot_log_lock);
    sysThrot_log_append_locked(line, written);
    mutex_unlock(&sysThrot_log_lock);
}

static int sysThrot_log_proc_show(struct seq_file *m, void *v)
{
    size_t first_chunk;

    mutex_lock(&sysThrot_log_lock);
    if (!sysThrot_log_buffer || sysThrot_log_len == 0) {
        mutex_unlock(&sysThrot_log_lock);
        return 0;
    }

    first_chunk = min(sysThrot_log_len, SYS_THR_LOG_BUFFER_SIZE - sysThrot_log_head);
    seq_write(m, sysThrot_log_buffer + sysThrot_log_head, first_chunk);
    if (sysThrot_log_len > first_chunk)
        seq_write(m, sysThrot_log_buffer, sysThrot_log_len - first_chunk);

    mutex_unlock(&sysThrot_log_lock);
    return 0;
}

static int sysThrot_log_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, sysThrot_log_proc_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops sysThrot_log_proc_ops = {
    .proc_open = sysThrot_log_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct file_operations sysThrot_log_proc_ops = {
    .owner = THIS_MODULE,
    .open = sysThrot_log_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

int sysThrot_log_init(void)
{
    sysThrot_log_buffer = kzalloc(SYS_THR_LOG_BUFFER_SIZE, GFP_KERNEL);
    if (!sysThrot_log_buffer)
        return -ENOMEM;

    sysThrot_log_proc_entry = proc_create(SYS_THR_LOG_NAME, 0444, NULL, &sysThrot_log_proc_ops);
    if (!sysThrot_log_proc_entry) {
        kfree(sysThrot_log_buffer);
        sysThrot_log_buffer = NULL;
        return -ENOMEM;
    }

    sysThrot_log_head = 0;
    sysThrot_log_len = 0;
    sysThrot_log("%s: module pseudo log initialized at /proc/%s", MODNAME, SYS_THR_LOG_NAME);

    return 0;
}

void sysThrot_log_cleanup(void)
{
    if (sysThrot_log_proc_entry) {
        remove_proc_entry(SYS_THR_LOG_NAME, NULL);
        sysThrot_log_proc_entry = NULL;
    }

    kfree(sysThrot_log_buffer);
    sysThrot_log_buffer = NULL;
    sysThrot_log_head = 0;
    sysThrot_log_len = 0;
}
