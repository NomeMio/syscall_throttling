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
#include "sysThrot_ioctl.h"
#include "sysThrot_store.h"



extern int installProbe(int syscall_id);
extern int removeProbe(int syscall_id);

#define DEVICE_NAME "sysThrot_dev"


int check_root_user(void){
    if (current_uid().val == 0) {
        return 1; // User is root
    }
    return 0; // User is not root
}


long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    switch(cmd) {
        case sysThrot_IOC_REGISTER_USER:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_register_user((TYPE_OF_DATA_PASSED_TO_IOCTL_USER)arg);
        case sysThrot_IOC_DEREGISTER_USER:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_deregister_user((TYPE_OF_DATA_PASSED_TO_IOCTL_USER)arg);
        case sysThrot_IOC_REGISTER_PROGRAM:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_register_program((TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)arg);
        case sysThrot_IOC_DEREGISTER_PROGRAM:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_deregister_program((TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)arg);
        case sysThrot_IOC_REGISTER_SYSCALL:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_register_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL )arg);
        case sysThrot_IOC_DEREGISTER_SYSCALL:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_deregister_syscall((TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL )arg);
        case sysThrot_IOC_TURN_ON:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_turn_on();
        case sysThrot_IOC_TURN_OFF:
            if (!check_root_user()) {
                return -EPERM;
            }
            return sysThrot_turn_off();
        default:
            LOG(LOG_IOCTL,"SysThrot: Invalid ioctl command %d", cmd);
            return -EINVAL;
    }
}





int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){

    int *user_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!user_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for user ID");
        return -ENOMEM;
    }
    if(copy_from_user(user_id_ptr, (int __user *)user_id, sizeof(int))) {
        kfree(user_id_ptr);
        LOG(LOG_IOCTL,"SysThrot: Failed to copy user ID from user space");
        return -EFAULT;
    }
    int ret = add_user_to_store(sysThrot_dev.store, user_id_ptr);


    if (!ret){
        LOG(LOG_IOCTL,"SysThrot: Registered user with ID %d", *user_id_ptr);
        return 0;
    } else if (ret == -EEXIST) {
        LOG(LOG_IOCTL,"SysThrot: User with ID %d is already registered", *user_id_ptr);
        kfree(user_id_ptr);
     }else{
        LOG(LOG_IOCTL,"SysThrot: Failed to register user with ID %d (err=%d)", *user_id_ptr, ret);
        kfree(user_id_ptr);
    }
    return ret;
}

int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id){
    int *user_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!user_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for user ID");
        return -ENOMEM;
    }
    if(copy_from_user(user_id_ptr, (int __user *)user_id, sizeof(int))) {
        kfree(user_id_ptr);
        LOG(LOG_IOCTL,"SysThrot: Failed to copy user ID from user space");
        return -EFAULT;
    }
    int ret = remove_user_from_store(sysThrot_dev.store, user_id_ptr);
    
    if (!ret)
        LOG(LOG_IOCTL,"SysThrot: Deregistered user with ID %d", *user_id_ptr);
    else
        LOG(LOG_IOCTL,"SysThrot: User with ID %d not registered", *user_id_ptr);

    kfree(user_id_ptr);
    return ret;
}

int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    char *user_id_ptr = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
    if (!user_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for program name");
        return -ENOMEM;
    }
    int res=strncpy_from_user(user_id_ptr, (char __user *)program_name, TASK_COMM_LEN);
    if (res<=0) {
        LOG(LOG_IOCTL,"SysThrot: Failed to copy program name from user space");
        kfree(user_id_ptr);
        return res;
    }
    
    res= add_program_to_store(sysThrot_dev.store, user_id_ptr);
    if (!res){
        LOG(LOG_IOCTL,"SysThrot: Registered program with name %s", user_id_ptr);
        return 0;
    } else if (res == -EEXIST) {
        LOG(LOG_IOCTL,"SysThrot: Program with name %s is already registered", user_id_ptr);
        kfree(user_id_ptr);
    }else{
        LOG(LOG_IOCTL,"SysThrot: Failed to register program with name %s (err=%d)", user_id_ptr, res);
        kfree(user_id_ptr);

    }
    return res;
}

int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
    char *user_id_ptr = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
    if (!user_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for program name");

        return -ENOMEM;
    }
    int res=strncpy_from_user(user_id_ptr, (char __user *)program_name, TASK_COMM_LEN);
    if (res<=0) {
        LOG(LOG_IOCTL,"SysThrot: Failed to copy program name from user space");
        kfree(user_id_ptr);
        return res;
    }
    
    res= remove_program_from_store(sysThrot_dev.store, user_id_ptr);
    if (!res){
        LOG(LOG_IOCTL,"SysThrot: Removed program with name %s", user_id_ptr);
        kfree(user_id_ptr);
        return 0;
    } else if (res == -EEXIST) {
        LOG(LOG_IOCTL,"SysThrot: Program with name %s is not registered", user_id_ptr);
        kfree(user_id_ptr);

     }else{
        LOG(LOG_IOCTL,"SysThrot: Failed to remove program with name %s (err=%d)", user_id_ptr, res);
        kfree(user_id_ptr);

    }
    return res;
}


int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int *syscall_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!syscall_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for syscall ID");
        return -ENOMEM;
    }
    if(copy_from_user(syscall_id_ptr, (int __user *)syscall_id, sizeof(int))) {
        kfree(syscall_id_ptr);
        LOG(LOG_IOCTL,"SysThrot: Failed to copy syscall ID from user space");
        return -EFAULT;
    }
    int reuslt=installProbe(*syscall_id_ptr);
    kfree(syscall_id_ptr);
    return reuslt;
}

int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    int *syscall_id_ptr = kmalloc(sizeof(int), GFP_KERNEL);
    if (!syscall_id_ptr) {
        LOG(LOG_IOCTL,"SysThrot: Failed to allocate memory for syscall ID");
        return -ENOMEM;
    }
    if(copy_from_user(syscall_id_ptr, (int __user *)syscall_id, sizeof(int))) {
        kfree(syscall_id_ptr);
        LOG(LOG_IOCTL,"SysThrot: Failed to copy syscall ID from user space");
        return -EFAULT;
    }
    int reuslt=removeProbe(*syscall_id_ptr);
    kfree(syscall_id_ptr);
    return reuslt;
}
