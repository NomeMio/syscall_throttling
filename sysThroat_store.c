#include "sysThrot.h"

#define MODNAME "SYSTHROT"
#include <stdio.h>
#include <linux/mutex.h>


#define TABLE_SIZE 128
#define MASK 0x7F
#define MAX_KICKS 64

    
struct _sysThrot_Store {
    CuckooHash syscall_user_map; 
    CuckooHash syscall_program_map; 
};

typedef struct {
    void *table[2][TABLE_SIZE];
    int occupied[2][TABLE_SIZE];
    struct mutex lock;
} CuckooHash;





void init_map(CuckooHash *map) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        map->occupied[0][i] = 0;
        map->occupied[1][i] = 0;
    }
    mutex_init(&map->lock);
}


void destroy_map(CuckooHash *map) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        if (map->occupied[0][i]) {
            kfree(map->table[0][i]);
        }
        if (map->occupied[1][i]) {
            kfree(map->table[1][i]);
        }
    }
    mutex_destroy(&map->lock);
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

int contains(CuckooHash *map, int key, void *item, int compare(void *a, void *b)) {
    unsigned int h1 = hash1(key);
    if (map->occupied[0][h1] && compare(map->table[0][h1],item) ) return 0;
    
    unsigned int h2 = hash2(key);
    if (map->occupied[1][h2] && compare(map->table[1][h2],item)) return 0;
    
    return -1;
}

int insert(CuckooHash *map, int key, void *item, int compare(void *a, void *b)) {
    mutex_lock(&map->lock);
    if (contains(map, key, item, compare) == 0) {
        mutex_unlock(&map->lock);
        return 0;
    }

    int current_key = key;
    void *current_item = item;
    for (int i = 0; i < MAX_KICKS; i++) {
        unsigned int h1 = hash1(current_key);
        if (!map->occupied[0][h1]) {
            map->table[0][h1] = current_item;
            map->occupied[0][h1] = 1;
            mutex_unlock(&map->lock);
            return 0;
        }

        int temp = map->table[0][h1];
        map->table[0][h1] = current_item;
        current_item = temp;

        unsigned int h2 = hash2(current_key);
        if (!map->occupied[1][h2]) {
            map->table[1][h2] = current_item;
            map->occupied[1][h2] = 1;
            mutex_unlock(&map->lock);
            return 0;
        }

        temp = map->table[1][h2];
        map->table[1][h2] = current_item;
        current_item = temp;
    }
    mutex_unlock(&map->lock);
    return -1;
}

int remove(CuckooHash *map, int key, void *item, int compare(void *a, void *b)) {
    mutex_lock(&map->lock);
    unsigned int h1 = hash1(key);
    if (map->occupied[0][h1] && compare(map->table[0][h1], item)) {
        map->occupied[0][h1] = 0;
        kfree(map->table[0][h1]);
        mutex_unlock(&map->lock);
        return 0;
    }

    unsigned int h2 = hash2(key);
    if (map->occupied[1][h2] && compare(map->table[1][h2], item)) {
        map->occupied[1][h2] = 0;
        kfree(map->table[1][h2]);
        mutex_unlock(&map->lock);
        return 0;
    }
    mutex_unlock(&map->lock);
    return -1;
}

int init_sysThrot_store(struct _sysThrot_Store **store){
    *store = kmalloc(sizeof(struct _sysThrot_Store), GFP_KERNEL);
    if (!*store) {
        printk("%s: Failed to allocate memory for sysThrot store\n", MODNAME);
        return -ENOMEM;
    }
    init_map(&(*store)->syscall_user_map);
    init_map(&(*store)->syscall_program_map);
    return 0;
}


int destroy_sysThrot_store(struct _sysThrot_Store *store){
    if (!store) {
        printk("%s: Invalid sysThrot store pointer\n", MODNAME);
        return -EINVAL;
    }
    destroy_map(&store->syscall_user_map);
    destroy_map(&store->syscall_program_map);
    kfree(store);
    return 0;
}



int getKeyFromUserId(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id) {
    return (int)user_id;
}

int getKeyFromProgramName(const char *program_name) {
    int hash = 0;
    while (*program_name) {
        hash = (hash * 31 + *program_name) % 0xFFFFFFFF;
        program_name++;
    }
    return hash;
}

int compareUser(void *a, void *b) {
    return *(int *)a == *(int *)b;
}

int compareProgram(void *a, void *b) {
    return strcmp((char *)a, (char *)b) == 0;
}

int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int key = getKeyFromUserId(user_id);
    return insert(&store->syscall_user_map, key, user_id, compareUser);
}
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int key = getKeyFromUserId(user_id);
    return remove(&store->syscall_user_map, key, user_id, compareUser);
}

int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id)
{
    int key = getKeyFromUserId(user_id);
    return contains(&store->syscall_user_map, key, user_id, compareUser);
}
int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    int key = getKeyFromProgramName(program_id);
    return insert(&store->syscall_program_map, key, program_id, compareProgram);
}
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    int key = getKeyFromProgramName(program_id);
    return remove(&store->syscall_program_map, key, program_id, compareProgram);
}
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    int key = getKeyFromProgramName(program_id);
    return contains(&store->syscall_program_map, key, program_id, compareProgram);
}




