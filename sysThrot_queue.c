#include <linux/slab.h>
#include "sysThrot_queue.h"


LIST_HEAD(global_queue);
static struct kmem_cache *queue_cache;

spinlock_t queue_lock;

int epoche_max_elemesnts = 1;
int current_in_queue = 0;

int init_queue(void) {
    epoche_max_elemesnts = sysThrot_dev.max_syscalls_for_epoch;
    queue_cache = kmem_cache_create("queue_cache",
                                    sizeof(struct _queue_elem),
                                    0, 
                                    SLAB_HWCACHE_ALIGN, 
                                    NULL);
    
    if (!queue_cache)
        return -ENOMEM;
    spin_lock_init(&queue_lock);
    return 0;
}

int add_to_queue(int *wake_up_flag) {
    struct _queue_elem *data = kmem_cache_alloc(queue_cache, GFP_KERNEL);
    if (!data)
        return -ENOMEM;
    data->wake_up_flag = wake_up_flag;
    current_in_queue++;

    spin_lock(&queue_lock);
    list_add_tail(&data->node, &global_queue);
    spin_unlock(&queue_lock);
    LOG(LOG_QUEUE, "Added thread to queue, current queue size: %d", current_in_queue);
    return 0;
}

int unqueue(int n) {
    int missing = n;
    struct _queue_elem *data, *tmp;

    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        if (missing <= 0)
            break;
        *data->wake_up_flag = 1; 
        list_del(&data->node);
        kmem_cache_free(queue_cache, data);
        missing--;
    }
    current_in_queue -= (n - missing);
    spin_unlock(&queue_lock);
    return n - missing;
}

int wake_up_queue(void) {
    struct _queue_elem *data, *tmp;
    int count = 0;

    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        *data->wake_up_flag = 1; 
        count++;
    }
    spin_unlock(&queue_lock);
    return count;
}

int destroy_queue(void) {
    struct _queue_elem *data, *tmp;
    int count = 0;
    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        list_del(&data->node);
        kmem_cache_free(queue_cache, data);
        count++;
    }
    spin_unlock(&queue_lock);
    return count;
}