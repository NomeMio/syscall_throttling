
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include "sysThrot.h"
#define MODNAME "SYSTHROT"


#define TABLE_SIZE 128
#define MASK 0x7F
#define MAX_KICKS 64

    

typedef struct {
    void *table[2][TABLE_SIZE];
    int occupied[2][TABLE_SIZE];
} CuckooHash;

struct _sysThrot_Store {
    CuckooHash __rcu *syscall_user_map;
    CuckooHash __rcu *syscall_program_map;
    struct mutex write_lock;
};

/* Forward declarations for functions implemented in this translation unit. */
CuckooHash *create_map(void);
void destroy_map(CuckooHash *map);
CuckooHash *clone_map(const CuckooHash *src, void *(*clone_item)(const void *item));

unsigned int hash1(int key);
unsigned int hash2(int key);

int contains(const CuckooHash *map, int key, const void *item, int compare(const void *a, const void *b));
int insert(CuckooHash *map, int key, void *item, int compare(const void *a, const void *b));
int remove(CuckooHash *map, int key, const void *item, int compare(const void *a, const void *b));

int init_sysThrot_store(struct _sysThrot_Store **store);
int destroy_sysThrot_store(struct _sysThrot_Store *store);

int getKeyFromUserId(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int getKeyFromProgramName(const char *program_name);

int compareUser(const void *a, const void *b);
int compareProgram(const void *a, const void *b);
void *cloneUser(const void *item);
void *cloneProgram(const void *item);

int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);

int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);




CuckooHash *create_map(void) {
    CuckooHash *map = kmalloc(sizeof(*map), GFP_KERNEL);
    int i;

    if (!map)
        return NULL;

    for (i = 0; i < TABLE_SIZE; i++) {
        map->occupied[0][i] = 0;
        map->occupied[1][i] = 0;
        map->table[0][i] = NULL;
        map->table[1][i] = NULL;
    }

    return map;
}


void destroy_map(CuckooHash *map) {
    int i;

    if (!map)
        return;

    for (i = 0; i < TABLE_SIZE; i++) {
        if (map->occupied[0][i]) {
            kfree(map->table[0][i]);
        }
        if (map->occupied[1][i]) {
            kfree(map->table[1][i]);
        }
    }
    kfree(map);
}   


CuckooHash *clone_map(const CuckooHash *src, void *(*clone_item)(const void *item)) {
    CuckooHash *dst = create_map();
    int i;

    if (!dst)
        return NULL;

    for (i = 0; i < TABLE_SIZE; i++) {
        if (src->occupied[0][i]) {
            dst->table[0][i] = clone_item(src->table[0][i]);
            if (!dst->table[0][i])
                goto fail;
            dst->occupied[0][i] = 1;
        }
        if (src->occupied[1][i]) {
            dst->table[1][i] = clone_item(src->table[1][i]);
            if (!dst->table[1][i])
                goto fail;
            dst->occupied[1][i] = 1;
        }
    }

    return dst;

fail:
    destroy_map(dst);
    return NULL;
}


unsigned int hash1(int key) {
    unsigned int x = (unsigned int)key;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x & MASK;
}

unsigned int hash2(int key) {
    unsigned int x = (unsigned int)key;
    x = ((x >> 16) ^ x) * 0x3335b369;
    x = ((x >> 16) ^ x) * 0x3335b369;
    x = (x >> 16) ^ x;
    return x & MASK;
}

int contains(const CuckooHash *map, int key, const void *item, int compare(const void *a, const void *b)) {
    unsigned int h1 = hash1(key);
    if (map->occupied[0][h1] && compare(map->table[0][h1],item) ) return 0;
    
    unsigned int h2 = hash2(key);
    if (map->occupied[1][h2] && compare(map->table[1][h2],item)) return 0;
    
    return -1;
}

int insert(CuckooHash *map, int key, void *item, int compare(const void *a, const void *b)) {
    if (contains(map, key, item, compare) == 0) {
        return -EEXIST;
    }

    int current_key = key;
    void *current_item = item;
    for (int i = 0; i < MAX_KICKS; i++) {
        unsigned int h1 = hash1(current_key);
        if (!map->occupied[0][h1]) {
            map->table[0][h1] = current_item;
            map->occupied[0][h1] = 1;
            return 0;
        }

        void *temp = map->table[0][h1];
        map->table[0][h1] = current_item;
        current_item = temp;

        unsigned int h2 = hash2(current_key);
        if (!map->occupied[1][h2]) {
            map->table[1][h2] = current_item;
            map->occupied[1][h2] = 1;
            return 0;
        }

        temp = map->table[1][h2];
        map->table[1][h2] = current_item;
        current_item = temp;
    }
    return -ENOSPC;
}

int remove(CuckooHash *map, int key, const void *item, int compare(const void *a, const void *b)) {
    unsigned int h1 = hash1(key);
    if (map->occupied[0][h1] && compare(map->table[0][h1], item)) {
        map->occupied[0][h1] = 0;
        kfree(map->table[0][h1]);
        map->table[0][h1] = NULL;
        return 0;
    }

    unsigned int h2 = hash2(key);
    if (map->occupied[1][h2] && compare(map->table[1][h2], item)) {
        map->occupied[1][h2] = 0;
        kfree(map->table[1][h2]);
        map->table[1][h2] = NULL;
        return 0;
    }
    return -1;
}

int init_sysThrot_store(struct _sysThrot_Store **store){
    CuckooHash *user_map;
    CuckooHash *program_map;

    *store = kmalloc(sizeof(struct _sysThrot_Store), GFP_KERNEL);
    if (!*store) {
        printk("%s: Failed to allocate memory for sysThrot store\n", MODNAME);
        return -ENOMEM;
    }

    user_map = create_map();
    program_map = create_map();
    if (!user_map || !program_map) {
        destroy_map(user_map);
        destroy_map(program_map);
        kfree(*store);
        return -ENOMEM;
    }

    mutex_init(&(*store)->write_lock);
    rcu_assign_pointer((*store)->syscall_user_map, user_map);
    rcu_assign_pointer((*store)->syscall_program_map, program_map);
    return 0;
}


int destroy_sysThrot_store(struct _sysThrot_Store *store){
    CuckooHash *user_map;
    CuckooHash *program_map;

    if (!store) {
        printk("%s: Invalid sysThrot store pointer\n", MODNAME);
        return -EINVAL;
    }

    mutex_lock(&store->write_lock);
    user_map = rcu_dereference_protected(store->syscall_user_map, 1);
    program_map = rcu_dereference_protected(store->syscall_program_map, 1);
    RCU_INIT_POINTER(store->syscall_user_map, NULL);
    RCU_INIT_POINTER(store->syscall_program_map, NULL);
    mutex_unlock(&store->write_lock);

    synchronize_rcu();
    destroy_map(user_map);
    destroy_map(program_map);
    mutex_destroy(&store->write_lock);
    kfree(store);
    return 0;
}



int getKeyFromUserId(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id) {
    return *user_id;
}

int getKeyFromProgramName(const char *program_name) {
    int hash = 0;
    while (*program_name) {
        hash = (hash * 31 + *program_name) % 0xFFFFFFFF;
        program_name++;
    }
    return hash;
}

int compareUser(const void *a, const void *b) {
    return *(int *)a == *(int *)b;
}

int compareProgram(const void *a, const void *b) {
    return strcmp((char *)a, (char *)b) == 0;
}

void *cloneUser(const void *item) {
    int *dup = kmalloc(sizeof(int), GFP_KERNEL);

    if (!dup)
        return NULL;

    *dup = *(int *)item;
    return dup;
}

void *cloneProgram(const void *item) {
    return kstrdup((const char *)item, GFP_KERNEL);
}

int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    CuckooHash *old_map;
    CuckooHash *new_map;
    int key = getKeyFromUserId(user_id);

    mutex_lock(&store->write_lock);
    old_map = rcu_dereference_protected(store->syscall_user_map, 1);
    if (contains(old_map, key, user_id, compareUser) == 0) {
        mutex_unlock(&store->write_lock);
        return -EEXIST;
    }

    new_map = clone_map(old_map, cloneUser);
    if (!new_map) {
        mutex_unlock(&store->write_lock);
        return -ENOMEM;
    }

    if (insert(new_map, key, user_id, compareUser) != 0) {
        destroy_map(new_map);
        mutex_unlock(&store->write_lock);
        return -ENOSPC;
    }

    rcu_assign_pointer(store->syscall_user_map, new_map);
    mutex_unlock(&store->write_lock);

    synchronize_rcu();
    destroy_map(old_map);
    return 0;
}
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    CuckooHash *old_map;
    CuckooHash *new_map;
    int key = getKeyFromUserId(user_id);

    mutex_lock(&store->write_lock);
    old_map = rcu_dereference_protected(store->syscall_user_map, 1);
    if (contains(old_map, key, user_id, compareUser) != 0) {
        mutex_unlock(&store->write_lock);
        return -EINVAL;
    }

    new_map = clone_map(old_map, cloneUser);
    if (!new_map) {
        mutex_unlock(&store->write_lock);
        return -ENOMEM;
    }

    if (remove(new_map, key, user_id, compareUser) != 0) {
        destroy_map(new_map);
        mutex_unlock(&store->write_lock);
        return -EINVAL;
    }

    rcu_assign_pointer(store->syscall_user_map, new_map);
    mutex_unlock(&store->write_lock);

    synchronize_rcu();
    destroy_map(old_map);
    return 0;
}

int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id)
{
    CuckooHash *map;
    int ret;
    int key = getKeyFromUserId(user_id);

    rcu_read_lock();
    map = rcu_dereference(store->syscall_user_map);
    ret = contains(map, key, user_id, compareUser);
    rcu_read_unlock();

    return ret;
}
int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    CuckooHash *old_map;
    CuckooHash *new_map;
    int key = getKeyFromProgramName(program_id);

    mutex_lock(&store->write_lock);
    old_map = rcu_dereference_protected(store->syscall_program_map, 1);
    if (contains(old_map, key, program_id, compareProgram) == 0) {
        mutex_unlock(&store->write_lock);
        return -EEXIST;
    }

    new_map = clone_map(old_map, cloneProgram);
    if (!new_map) {
        mutex_unlock(&store->write_lock);
        return -ENOMEM;
    }

    if (insert(new_map, key, program_id, compareProgram) != 0) {
        destroy_map(new_map);
        mutex_unlock(&store->write_lock);
        return -ENOSPC;
    }

    rcu_assign_pointer(store->syscall_program_map, new_map);
    mutex_unlock(&store->write_lock);

    synchronize_rcu();
    destroy_map(old_map);
    return 0;
}
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    CuckooHash *old_map;
    CuckooHash *new_map;
    int key = getKeyFromProgramName(program_id);

    mutex_lock(&store->write_lock);
    old_map = rcu_dereference_protected(store->syscall_program_map, 1);
    if (contains(old_map, key, program_id, compareProgram) != 0) {
        mutex_unlock(&store->write_lock);
        return -EINVAL;
    }

    new_map = clone_map(old_map, cloneProgram);
    if (!new_map) {
        mutex_unlock(&store->write_lock);
        return -ENOMEM;
    }

    if (remove(new_map, key, program_id, compareProgram) != 0) {
        destroy_map(new_map);
        mutex_unlock(&store->write_lock);
        return -EINVAL;
    }

    rcu_assign_pointer(store->syscall_program_map, new_map);
    mutex_unlock(&store->write_lock);

    synchronize_rcu();
    destroy_map(old_map);
    return 0;
}
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    CuckooHash *map;
    int ret;
    int key = getKeyFromProgramName(program_id);

    rcu_read_lock();
    map = rcu_dereference(store->syscall_program_map);
    ret = contains(map, key, program_id, compareProgram);
    rcu_read_unlock();

    return ret;
}




