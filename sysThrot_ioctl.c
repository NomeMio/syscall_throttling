#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include "./sysThrot.h"




extern int installProbe(int syscall_id);
extern int removeProbe(int syscall_id);

#define DEVICE_NAME "sysThrot_dev"



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
            return sysThrot_register_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL )arg);
        case sysThrot_IOC_DEREGISTER_SYSCALL:
            return sysThrot_deregister_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL )arg);
        default:
            AUDIT
            printk("%s: Invalid ioctl command\n", MODNAME);
            return -EINVAL;
    }
}


int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){

    int *user_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!user_id_ptr) {
        printk("%s: Failed to allocate memory for user ID\n", MODNAME);
        return -ENOMEM;
    }
    if(copy_from_user(user_id_ptr, (int __user *)user_id, sizeof(int))) {
        kfree(user_id_ptr);
        printk("%s: Failed to copy user ID from user space\n", MODNAME);
        return -EFAULT;
    }
    int ret = add_user_to_store(sysThrot_dev.store, user_id_ptr);


    if (!ret){
        AUDIT  
        printk("%s: Registered user with ID %d\n", MODNAME, *user_id_ptr);
        return 0;
    } else if (ret == -EEXIST) {
        AUDIT
        printk("%s: User with ID %d is already registered\n", MODNAME, *user_id_ptr);
        kfree(user_id_ptr);
     }else{
        AUDIT
        printk("%s: Failed to register user with ID %d (err=%d)\n", MODNAME, *user_id_ptr, ret);
        kfree(user_id_ptr);
    }
    return ret;
}

int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int *user_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!user_id_ptr) {
        printk("%s: Failed to allocate memory for user ID\n", MODNAME);
        return -ENOMEM;
    }
    if(copy_from_user(user_id_ptr, (int __user *)user_id, sizeof(int))) {
        kfree(user_id_ptr);
        printk("%s: Failed to copy user ID from user space\n", MODNAME);
        return -EFAULT;
    }
    int ret = remove_user_from_store(sysThrot_dev.store, user_id_ptr);
    
    if (!ret)
        printk("%s: Deregistered user with ID %d\n", MODNAME, *user_id_ptr);
    else
        printk("%s: User with ID %d not registered\n", MODNAME, *user_id_ptr);

    kfree(user_id_ptr);
    return ret;
}

int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    char *user_id_ptr = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
    if (!user_id_ptr) {
        printk("%s: Failed to allocate memory for program name\n", MODNAME);
        return -ENOMEM;
    }
    int res=strncpy_from_user(user_id_ptr, (char __user *)program_name, TASK_COMM_LEN);
    if (res<=0) {
        AUDIT
        printk("%s: Failed to allocate memory for user ID\n", MODNAME);
        return res;
    }
    
    res= add_program_to_store(sysThrot_dev.store, user_id_ptr);
    if (!res){
        AUDIT  
        printk("%s: Registered program with name %s\n", MODNAME, user_id_ptr);
        return 0;
    } else if (res == -EEXIST) {
        AUDIT
        printk("%s: Program with name %s is already registered\n", MODNAME, user_id_ptr);
        kfree(user_id_ptr);

     }else{
        AUDIT
        printk("%s: Failed to register program with name %s (err=%d)\n", MODNAME, user_id_ptr, res);
        kfree(user_id_ptr);

    }
    return res;
}

int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    char *user_id_ptr = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
    if (!user_id_ptr) {
        printk("%s: Failed to allocate memory for program name\n", MODNAME);
        return -ENOMEM;
    }
    int res=strncpy_from_user(user_id_ptr, (char __user *)program_name, TASK_COMM_LEN);
    if (res<=0) {
        AUDIT
        printk("%s: Failed to allocate memory for user ID\n", MODNAME);
        return res;
    }
    
    res= remove_program_from_store(sysThrot_dev.store, user_id_ptr);
    if (!res){
        AUDIT  
        printk("%s: removed program with name %s\n", MODNAME, user_id_ptr);
        kfree(user_id_ptr);
        return 0;
    } else if (res == -EEXIST) {
        AUDIT
        printk("%s: Program with name %s is not registered\n", MODNAME, user_id_ptr);
        kfree(user_id_ptr);

     }else{
        AUDIT
        printk("%s: Failed to remove program with name %s (err=%d)\n", MODNAME, user_id_ptr, res);
        kfree(user_id_ptr);

    }
    return res;
}


int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int *syscall_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!syscall_id_ptr) {
        printk("%s: Failed to allocate memory for syscall ID\n", MODNAME);
        return -ENOMEM;
    }
    if(copy_from_user(syscall_id_ptr, (int __user *)syscall_id, sizeof(int))) {
        kfree(syscall_id_ptr);
        printk("%s: Failed to copy syscall ID from user space\n", MODNAME);
        return -EFAULT;
    }
    int reuslt=installProbe(*syscall_id_ptr);
    kfree(syscall_id_ptr);
    return reuslt;
}

int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int *syscall_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!syscall_id_ptr) {
        printk("%s: Failed to allocate memory for syscall ID\n", MODNAME);
        return -ENOMEM;
    }
    if(copy_from_user(syscall_id_ptr, (int __user *)syscall_id, sizeof(int))) {
        kfree(syscall_id_ptr);
        printk("%s: Failed to copy syscall ID from user space\n", MODNAME);
        return -EFAULT;
    }
    int reuslt=removeProbe(*syscall_id_ptr);
    kfree(syscall_id_ptr);
    return reuslt;
}
