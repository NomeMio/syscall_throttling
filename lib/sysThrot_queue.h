#include "syscalls.h"

#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/spinlock.h>

struct _queue_elem{
    int *wake_up_flag;
    struct list_head node;
};
LIST_HEAD(global_queue);
static struct kmem_cache *queue_cache;


spinlock_t queue_lock ;


int epoche_max_elemesnts = 1;



int init_my_data_cache(int max_elements);
int add_to_queue(int *wake_up_flag);
int unqueue(int n);
int set_wake_all_list(void);
int free_all_list(void);



int init_my_data_cache(int max_elements) {
    epoche_max_elemesnts = max_elements;
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
    
    spin_lock(&queue_lock);
    list_add_tail(&data->node, &global_queue);
    spin_unlock(&queue_lock);

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
    spin_unlock(&queue_lock);
    return n - missing;
}

int set_wake_all_list(){
    
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
int free_all_list(){
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
