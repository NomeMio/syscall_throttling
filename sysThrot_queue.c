#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
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
        return NULL;

    data->exited = 0;
    data->wake_up_flag = 0;

    spin_lock(&queue_lock);
    if (sysThrot_dev.working == 0) { // this makes sure that monitor gets turned off correctly
        spin_unlock(&queue_lock);
        kmem_cache_free(queue_cache, data);
        return (int *)1;
    }

    current_in_queue++;
    list_add_tail(&data->node, &global_queue);
    spin_unlock(&queue_lock);

    return &data->wake_up_flag;
}

int unqueue(int n) {
    int missing = n;
    struct _queue_elem *data, *tmp;

    spin_lock(&queue_lock);
    list_for_each_entry_safe(data, tmp, &global_queue, node) {
        if (data->exited) {
            spin_lock(&junk_lock);
            list_move_tail(&data->node, &junk_list);
            spin_unlock(&junk_lock);
            current_in_queue--;
            continue;
        }
        if (missing <= 0)
            break;
        data->wake_up_flag = 1;
        spin_lock(&junk_lock);
        list_move_tail(&data->node, &junk_list);
        spin_unlock(&junk_lock);
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
    struct _queue_elem *data = container_of(wakeup_flag, struct _queue_elem, wake_up_flag);
    data->exited = 1;
}

int wake_up_queue(void) {
    int count = 0;
    struct _queue_elem *data;

    spin_lock(&queue_lock);
    spin_lock(&junk_lock);

    list_for_each_entry(data, &global_queue, node) {
        data->wake_up_flag = 1;
        count++;
    }
    list_splice_tail_init(&global_queue, &junk_list);
    current_in_queue = 0;

    spin_unlock(&junk_lock);
    spin_unlock(&queue_lock);
    return count;
}

int destroy_queue(void) {
    struct _queue_elem *data, *tmp;   
    //At this point, wake_up_queue() should have been called, so the global_queue should be empty, and all the threads outisde the module.
    free_junk();
    kmem_cache_destroy(queue_cache);
    return 0;
}