#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <linux/syscalls.h>
//#include "./include/vtpmo.h"
#include "./sysThrot.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Syscall Throttling Module");


#define MODNAME "SYSTHROT"
int major_number;
int minor_number=0;
unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);

//Forward declarations
int device_driver_init(void);
int device_driver_cleanup(void);
int sysThrot_init(void);
void sysThrot_cleanup(void);

    
//Device driver stuff
#define DEVICE_NAME "sysThrot_dev"


struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = sysThrot_ioctl,
};


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
static struct generic_list syscall_list = {.head = NULL, .data_size = sizeof(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL), .name = "syscall"};

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




struct sysThrot_driver
{

    struct file_operations fops;
    struct cdev cdev;
};



int device_driver_init(void){
    dev_t dev;
    int result;

    mutex_init(&user_list.lock);
    mutex_init(&program_list.lock);
    mutex_init(&syscall_list.lock);

    result=alloc_chrdev_region(&dev, minor_number, 1, DEVICE_NAME);
    major_number=MAJOR(dev);
    if (result < 0) {
        printk(KERN_ALERT "%s: Failed to allocate a major number\n", MODNAME);
        return result;
    }


    return result;
}


int device_driver_cleanup(void){
    generic_list_destroy(&user_list);
    generic_list_destroy(&program_list);
    generic_list_destroy(&syscall_list);
    unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
    return 0;
}

long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    switch(cmd) {
        case sysThrot_IOC_REGISTER_USER:
            return sysThrot_register_user((TYPE_OF_DATA_PASSED_TO_IOCTL_USER)arg);
        case sysThrot_IOC_DEREGISTER_USER:
            return sysThrot_deregister_user((TYPE_OF_DATA_PASSED_TO_IOCTL_USER)arg);
        case sysThrot_IOC_REGISTER_PROGRAM:
            return sysThrot_register_program((TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)arg);
        case sysThrot_IOC_DEREGISTER_PROGRAM:
            return sysThrot_deregister_program((TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)arg);
        case sysThrot_IOC_REGISTER_SYSCALL:
            return sysThrot_register_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)arg);
        case sysThrot_IOC_DEREGISTER_SYSCALL:
            return sysThrot_deregister_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)arg);
        default:
            printk("%s: Invalid ioctl command\n", MODNAME);
            return -EINVAL;
    }
}


int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int ret = generic_list_add(&user_list, &user_id);

    if (!ret)
        printk("%s: Registered user with ID %d\n", MODNAME, user_id);
    else if (ret == -EEXIST)
        printk("%s: User with ID %d is already registered\n", MODNAME, user_id);
    else
        printk("%s: Failed to register user with ID %d (err=%d)\n", MODNAME, user_id, ret);

    return ret;
}
int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int ret = generic_list_remove(&user_list, &user_id);

    if (!ret)
        printk("%s: Deregistered user with ID %d\n", MODNAME, user_id);
    else
        printk("%s: User with ID %d not registered\n", MODNAME, user_id);

    return ret;
}
int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    int ret = generic_list_add(&program_list, &program_id);

    if (!ret)
        printk("%s: Registered program with ID %d\n", MODNAME, program_id);
    else if (ret == -EEXIST)
        printk("%s: Program with ID %d is already registered\n", MODNAME, program_id);
    else
        printk("%s: Failed to register program with ID %d (err=%d)\n", MODNAME, program_id, ret);

    return ret;
}
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id){
    int ret = generic_list_remove(&program_list, &program_id);

    if (!ret)
        printk("%s: Deregistered program with ID %d\n", MODNAME, program_id);
    else
        printk("%s: Program with ID %d not registered\n", MODNAME, program_id);

    return ret;
}
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int ret = generic_list_add(&syscall_list, &syscall_id);

    if (!ret)
        printk("%s: Registered syscall with ID %d\n", MODNAME, syscall_id);
    else if (ret == -EEXIST)
        printk("%s: Syscall with ID %d is already registered\n", MODNAME, syscall_id);
    else
        printk("%s: Failed to register syscall with ID %d (err=%d)\n", MODNAME, syscall_id, ret);

    return ret;
}
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int ret = generic_list_remove(&syscall_list, &syscall_id);

    if (!ret)
        printk("%s: Deregistered syscall with ID %d\n", MODNAME, syscall_id);
    else
        printk("%s: Syscall with ID %d not registered\n", MODNAME, syscall_id);

    return ret;
}








int sysThrot_init(void) {
    int ret;

    printk("%s: initializing\n",MODNAME);
    
    ret = device_driver_init();
    if (ret < 0) {
        printk("%s: device driver init failed\n",MODNAME);
        return ret;
    }

    printk("%s: module correctly mounted\n",MODNAME);
    return 0;
}


void sysThrot_cleanup(void) {
    device_driver_cleanup();
    printk("%s: shutting down\n",MODNAME);
}




module_init(sysThrot_init);
module_exit(sysThrot_cleanup);
