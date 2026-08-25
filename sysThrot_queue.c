#include <linux/slab.h>
#include "sysThrot_queue.h"


LIST_HEAD(global_queue);
static struct kmem_cache *queue_cache;
LIST_HEAD(junk_list);
spinlock_t junk_lock;

spinlock_t queue_lock;

int current_in_queue = 0;

int init_queue(void) {
    queue_cache = kmem_cache_create("queue_cache",
                                    sizeof(struct _queue_elem),
                                    0, 
                                    SLAB_HWCACHE_ALIGN, 
                                    NULL);
    
    if (!queue_cache)
        return -ENOMEM;
    spin_lock_init(&queue_lock);
    spin_lock_init(&junk_lock);
    return 0;
}

int* add_to_queue(void) {
    struct _queue_elem *data = kmem_cache_alloc(queue_cache, GFP_KERNEL);
    if (!data)
        return 0;
    data->exited=0;
    data->wake_up_flag = 0;
    current_in_queue++;

    spin_lock(&queue_lock);
    list_add_tail(&data->node, &global_queue);
    spin_unlock(&queue_lock);
    LOG(LOG_QUEUE, "Added thread to queue, current queue size: %d", current_in_queue);
    return &data->wake_up_flag;
}

int unqueue(int n) {
    int missing = n;
    struct _queue_elem *data, *tmp;

    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        if (data->exited) {
            list_del(&data->node);
            kmem_cache_free(queue_cache, data);
            current_in_queue--;
            continue;
        }
        if (missing <= 0)
            break;
        data->wake_up_flag = 1; 
        //move to junk queue
        spin_lock(&junk_lock);
        list_move_tail(&data->node, &junk_list);
        spin_unlock(&junk_lock);
        //list_del(&data->node);
        //kmem_cache_free(queue_cache, data);
        missing--;
    }
    current_in_queue -= (n - missing);
    spin_unlock(&queue_lock);
    return n - missing;
}

void free_junk(void) {
    struct _queue_elem *data, *tmp;
    spin_lock(&junk_lock);
    list_for_each_entry_safe(data, tmp, &junk_list, node) {
        if (data->exited) {
            list_del(&data->node);
            kmem_cache_free(queue_cache, data);
        }
    }

    spin_unlock(&junk_lock);
}

void set_exited_flag(int *wakeup_flag) {
    struct _queue_elem *data= container_of(wakeup_flag, struct _queue_elem, wake_up_flag);
    data->exited = 1;
}

int wake_up_queue(void) {
    struct _queue_elem *data, *tmp;
    int count = 0;

    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        data->wake_up_flag = 1; 
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
    free_junk();
    kmem_cache_destroy(queue_cache);
    return count;
}