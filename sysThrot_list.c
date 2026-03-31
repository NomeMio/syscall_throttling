#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "sysThrot.h"

#define MODNAME "SYSTHROT"

struct generic_list_node {
    void *data;
    struct generic_list_node *next;
};

struct generic_list {
    struct generic_list_node *head;
    size_t data_size;
    struct mutex lock;
    const char *name;
};

static struct generic_list user_list = {.head = NULL, .data_size = sizeof(TYPE_OF_DATA_PASSED_TO_IOCTL_USER), .name = "user"};
static struct generic_list program_list = {.head = NULL, .data_size = sizeof(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM), .name = "program"};

static int program_list_find_locked(const char *program_name)
{
    struct generic_list_node *node = program_list.head;

    while (node) {
        if (!strcmp((const char *)node->data, program_name))
            return 1;
        node = node->next;
    }

    return 0;
}

static int program_list_add_from_user(const char __user *user_program_name)
{
    struct generic_list_node *new_node;
    char *program_name;
    //TODO DA METTERE QUALCHE MACRO PER LA SIZE
    program_name=kmalloc(256, GFP_KERNEL);
    int result = strncpy_from_user(program_name, user_program_name, 256);
    if (result < 0) {
        kfree(program_name);
        printk("%s: Failed to copy program name from user (err=%d)\n", MODNAME, result);
        return -1;
    }
    mutex_lock(&program_list.lock);
    if (program_list_find_locked(program_name)) {
        mutex_unlock(&program_list.lock);
        kfree(program_name);
        return -EEXIST;
    }

    new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node) {
        mutex_unlock(&program_list.lock);
        kfree(program_name);
        return -ENOMEM;
    }

    new_node->data = program_name;
    new_node->next = program_list.head;
    program_list.head = new_node;
    mutex_unlock(&program_list.lock);

    return 0;
}

static int program_list_remove_from_user(const char __user *user_program_name)
{
    struct generic_list_node *node;
    struct generic_list_node *prev = NULL;
    char *program_name;

    program_name=kmalloc(256, GFP_KERNEL);
    int result = strncpy_from_user(program_name, user_program_name, 256);
    if (result < 0) {
        kfree(program_name);
        printk("%s: Failed to copy program name from user (err=%d)\n", MODNAME, result);
        return -1;
    }

    mutex_lock(&program_list.lock);
    node = program_list.head;

    while (node) {
        if (!strcmp((const char *)node->data, program_name)) {
            if (prev)
                prev->next = node->next;
            else
                program_list.head = node->next;
            kfree(node->data);
            kfree(node);
            mutex_unlock(&program_list.lock);
            kfree(program_name);
            return 0;
        }
        prev = node;
        node = node->next;
    }

    mutex_unlock(&program_list.lock);
    kfree(program_name);
    return -ENOENT;
}

static int generic_list_find_locked(struct generic_list *list, const void *value)
{
    struct generic_list_node *node = list->head;

    while (node) {
        if (!memcmp(node->data, value, list->data_size))
            return 1;
        node = node->next;
    }

    return 0;
}

static int generic_list_add(struct generic_list *list, const void *value)
{
    struct generic_list_node *new_node;

    mutex_lock(&list->lock);
    if (generic_list_find_locked(list, value)) {
        mutex_unlock(&list->lock);
        return -EEXIST;
    }

    new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node) {
        mutex_unlock(&list->lock);
        return -ENOMEM;
    }

    new_node->data = kmalloc(list->data_size, GFP_KERNEL);
    if (!new_node->data) {
        kfree(new_node);
        mutex_unlock(&list->lock);
        return -ENOMEM;
    }

    memcpy(new_node->data, value, list->data_size);
    new_node->next = list->head;
    list->head = new_node;
    mutex_unlock(&list->lock);

    return 0;
}

static int generic_list_remove(struct generic_list *list, const void *value)
{
    struct generic_list_node *node;
    struct generic_list_node *prev = NULL;

    mutex_lock(&list->lock);
    node = list->head;

    while (node) {
        if (!memcmp(node->data, value, list->data_size)) {
            if (prev)
                prev->next = node->next;
            else
                list->head = node->next;
            kfree(node->data);
            kfree(node);
            mutex_unlock(&list->lock);
            return 0;
        }
        prev = node;
        node = node->next;
    }

    mutex_unlock(&list->lock);
    return -ENOENT;
}

static void generic_list_destroy(struct generic_list *list)
{
    struct generic_list_node *node;

    mutex_lock(&list->lock);
    node = list->head;
    while (node) {
        struct generic_list_node *next = node->next;
        kfree(node->data);
        kfree(node);
        node = next;
    }
    list->head = NULL;
    mutex_unlock(&list->lock);
    mutex_destroy(&list->lock);
}

void print_program_list(void){
    struct generic_list_node *node;
    mutex_lock(&program_list.lock);
    node = program_list.head;
    printk("%s: Registered programs:\n", MODNAME);
    while (node) {
        printk("%s\n", (char *)node->data);
        node = node->next;
    }
    mutex_unlock(&program_list.lock);
}

void init_lists(void){
    mutex_init(&user_list.lock);
    mutex_init(&program_list.lock);
}

void destroy_lists(void){
    generic_list_destroy(&user_list);
    generic_list_destroy(&program_list);
}

// User list operations
int user_list_add(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id)
{
    return generic_list_add(&user_list, &user_id);
}

int user_list_remove(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id)
{
    return generic_list_remove(&user_list, &user_id);
}

// Program list operations
int program_list_add(const char __user *user_program_name)
{
    return program_list_add_from_user(user_program_name);
}

int program_list_remove(const char __user *user_program_name)
{
    return program_list_remove_from_user(user_program_name);
}

int program_list_find(const char *program_name)
{
    return program_list_find_locked(program_name);
}
