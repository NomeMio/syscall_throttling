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
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <linux/syscalls.h>
#include <linux/ftrace.h>
#include <linux/delay.h>
#include <linux/cred.h>
//#include "./include/vtpmo.h"
#include "./sysThrot.h"

unsigned long cr0, cr4;

static inline void write_cr0_forced(unsigned long val){
        unsigned long __force_order;
        asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(__force_order));
}
static inline void protect_memory(void){
        write_cr0_forced(cr0);
}

static inline void unprotect_memory(void){
        write_cr0_forced(cr0 & ~X86_CR0_WP);
}

static inline void write_cr4_forced(unsigned long val){
        unsigned long __force_order;
        asm volatile("mov %0, %%cr4" : "+r"(val), "+m"(__force_order));
}

static inline void conditional_cet_disable(void){
#ifdef X86_CR4_CET
        if (cr4 & X86_CR4_CET)
                write_cr4_forced(cr4 & ~X86_CR4_CET);
#endif
}

static inline void conditional_cet_enable(void){
#ifdef X86_CR4_CET
        if (cr4 & X86_CR4_CET)
                write_cr4_forced(cr4);
#endif
}

static inline void being_sys_call_hacking(void){
        preempt_disable();
        cr0 = read_cr0();
        cr4 = native_read_cr4();
        conditional_cet_disable();
        unprotect_memory();
}

static inline void end_sys_call_hacking(void){
        protect_memory();
        conditional_cet_enable();
        preempt_enable();
}








MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Syscall Throttling Module");


#define MODNAME "SYSTHROT"
int major_number;
int minor_number=0;
//unsigned long sys_call_table_address = 0x0;
//module_param(sys_call_table_address, ulong, 0660);
unsigned long max_calls_monitor = 0;
module_param(max_calls_monitor, ulong, 0660);


unsigned long counter=0;

module_param(counter, ulong, 0660);


//Device driver stuff
#define DEVICE_NAME "sysThrot_dev"

const struct file_operations fops = {
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

struct sysThrot_driver
{
    int active_calls;
    int max_calls;
    char syscall_presence_bitmap[__NR_syscalls/8+1];
    int bitmap_size;
    struct file_operations fops;
    struct cdev cdev;
    struct class *device_class;
};

struct sysThrot_driver sysThrot_dev = {
    .fops = fops,
    .active_calls = 0,
    .bitmap_size=__NR_syscalls/8+1,
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




int device_driver_init(void){
    dev_t dev;
    int result;

    mutex_init(&user_list.lock);
    mutex_init(&program_list.lock);

    result=alloc_chrdev_region(&dev, minor_number, 1, DEVICE_NAME);
    major_number=MAJOR(dev);
    if (result < 0) {
        printk(KERN_ALERT "%s: Failed to allocate a major number\n", MODNAME);
        return result;
    }
    sysThrot_dev.device_class=class_create(DEVICE_NAME);
    if(IS_ERR(sysThrot_dev.device_class)){
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        printk(KERN_ALERT "%s: Failed to create device class\n", MODNAME);
        return PTR_ERR(sysThrot_dev.device_class);
    }
    if(device_create(sysThrot_dev.device_class, NULL, MKDEV(major_number, minor_number), NULL, DEVICE_NAME) == NULL){
        class_destroy(sysThrot_dev.device_class);
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        printk(KERN_ALERT "%s: Failed to create device\n", MODNAME);
        return -1;
    }
    cdev_init(&sysThrot_dev.cdev, &sysThrot_dev.fops);
    sysThrot_dev.cdev.owner = THIS_MODULE;
    result = cdev_add(&sysThrot_dev.cdev, MKDEV(major_number, minor_number), 1);
    if (result < 0) {
        device_destroy(sysThrot_dev.device_class, MKDEV(major_number, minor_number));
        class_destroy(sysThrot_dev.device_class);
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        printk(KERN_ALERT "%s: Failed to add cdev\n", MODNAME);
    }
    return result;
}


int device_driver_cleanup(void){
    generic_list_destroy(&user_list);
    generic_list_destroy(&program_list);
    cdev_del(&sysThrot_dev.cdev);
    device_destroy(sysThrot_dev.device_class, MKDEV(major_number, minor_number));
    class_destroy(sysThrot_dev.device_class);
    unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
    return 0;
}

long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    int syscall_id ;
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
            copy_from_user(&syscall_id, (TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL *)arg, sizeof(syscall_id));
            return sysThrot_register_syscall(syscall_id);
        case sysThrot_IOC_DEREGISTER_SYSCALL:
            copy_from_user(&syscall_id, (TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL *)arg, sizeof(syscall_id));
            return sysThrot_deregister_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)syscall_id);
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
int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    int ret = program_list_add_from_user((const char __user *)program_name);

    if (!ret)
        printk("%s: Registered program\n", MODNAME);
    else if (ret == -EEXIST)
        printk("%s: Program is already registered\n", MODNAME);
    else
        printk("%s: Failed to register program (err=%d)\n", MODNAME, ret);
    return ret;
}
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    int ret = program_list_remove_from_user((const char __user *)program_name);

    if (!ret)
        printk("%s: Deregistered program\n", MODNAME);
    else
        printk("%s: Program not registered\n", MODNAME);

    return ret;
}



// TODO: per implementarla realmente a run time non so come fare
static const char *  syscall_symbols[]  = {
    "__x64_sys_read",
    "__x64_sys_write",
    "__x64_sys_open", 
    "__x64_sys_close",
    "__x64_sys_stat"
};


void temp(struct pt_regs *regs) {
    struct task_struct *task = current;
    if (program_list_find_locked(task->comm)) {
       msleep(4000);
    }
}

asm(
".global my_stub\n"
"my_stub:\n"
"    pushq %rax\n" // Save all registers that the syscall needs
"    pushq %rdi\n"
"    pushq %rsi\n"
"    pushq %rdx\n"
"    pushq %rcx\n"
"    pushq %r11\n"
"    call temp\n"  // Call your C function
"    popq %r11\n"  // Restore everything exactly as it was
"    popq %rcx\n"
"    popq %rdx\n"
"    popq %rsi\n"
"    popq %rdi\n"
"    popq %rax\n"
"    ret\n"        // Return to the instruction AFTER your injected CALL
);
extern void my_stub(void);
int installProbe(int syscall_id){
    static struct kprobe kp;
    printk("%s: Installing kprobe for syscall (ID %d)\n", MODNAME, syscall_id);

    if (syscall_id < 0 || syscall_id >= ARRAY_SIZE(syscall_symbols))
        return -EINVAL;

    kp.symbol_name = syscall_symbols[syscall_id];
   
    if (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8))) {
        printk("%s: Syscall (ID %d) is already registered\n", MODNAME, syscall_id);
        return -EEXIST;
    }

     int result = register_kprobe(&kp);
    if (result) {
        printk("%s: Failed to register kprobe for syscall (ID %d), with error %d\n", MODNAME, syscall_id,result);
        return result;
    }  
    //sarebbe da sistemare per poi metterlo apposto
    unsigned long addr_sys = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    printk("%s: Probed syscall (ID %d) %s  at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);
    int INTS_LEN=5;
    char call_instruction[INTS_LEN];
    call_instruction[0]=0xE8; // opcode for CALL rel32
    int offset = (unsigned long)my_stub - addr_sys - INTS_LEN;
    memcpy(call_instruction + 1, &offset, sizeof(int));
    being_sys_call_hacking();
    memcpy((void *)addr_sys, call_instruction, INTS_LEN);
    end_sys_call_hacking();
    sysThrot_dev.syscall_presence_bitmap[syscall_id/8] |= (1 << (syscall_id % 8));
    return 1;
}



int removeProbe(int syscall_id){
    static struct kprobe kp;

    if (syscall_id < 0 || syscall_id >= ARRAY_SIZE(syscall_symbols))
        return -EINVAL; 
    if( sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8))==0 )
            return -EINVAL; 
    kp.symbol_name = syscall_symbols[syscall_id];
    int result = register_kprobe(&kp);
    if (result) {
        printk("%s: Failed to register kprobe for syscall (ID %d), with error %d\n", MODNAME, syscall_id,result);
        return -1;
    }   
    //sarebbe da sistemare per poi metterlo apposto
    unsigned long addr_sys = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    printk("%s: Probed syscall (ID %d) %s  at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);
    int INTS_LEN=5;
    char call_instruction[INTS_LEN];
    call_instruction[0]=0x90;
    call_instruction[1]=0x90;
    call_instruction[2]=0x90;
    call_instruction[3]=0x90;
    call_instruction[4]=0x90;
    being_sys_call_hacking();
    memcpy((void *)addr_sys, call_instruction, INTS_LEN);
    end_sys_call_hacking();
    sysThrot_dev.syscall_presence_bitmap[syscall_id/8] &= ~(1 << (syscall_id % 8));
    return 1;
}   
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int user=current_uid().val;
    struct task_struct *task = current;
    printk("%s: Current process is %s (PID %d)\n", MODNAME, task->comm, task->pid);
    int outcome=program_list_find_locked(task->comm);
    if (outcome)
        printk("%s: Current process is registered for syscall throttling\n", MODNAME);
    printk("%s: User with ID %d requests to register syscall with ID %d\n,", MODNAME, user, syscall_id);
    installProbe(syscall_id);
    return 1;
}
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
        int user=current_uid().val;
    if(removeProbe(syscall_id)<0){
        printk("%s: User with ID %d failed to deregister syscall with ID %d\n,", MODNAME, user, syscall_id);
        return -EINVAL;
    };
    printk("%s: User with ID %d is deregistering syscall with ID %d\n,", MODNAME, user, syscall_id);

    return 1;
}









int sysThrot_init(void) {
    int ret;
    sysThrot_dev.max_calls = max_calls_monitor;
    printk("%s: initializing\n",MODNAME);
    printk("%d concurrent calls allowed\n", max_calls_monitor);
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
