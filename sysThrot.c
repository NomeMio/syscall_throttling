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




#define  AUDIT if(1)

#define SUPPORTED_SYSCALLS 333




MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Syscall Throttling Module");


#define MODNAME "SYSTHROT"
int major_number;
int minor_number=0;

unsigned long max_calls_monitor = 0;
module_param(max_calls_monitor, ulong, 0660);



static const char *syscall_symbols[SUPPORTED_SYSCALLS];


//Device driver stuff
#define DEVICE_NAME "sysThrot_dev"

const struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = sysThrot_ioctl,
};


struct sysThrot_driver
{   
    struct _sysThrot_Store *store;
    char syscall_presence_bitmap[__NR_syscalls/8+1];
    int bitmap_size;
    unsigned long syscall_addresses[SUPPORTED_SYSCALLS];
    struct file_operations fops;
    struct cdev cdev;
    struct class *device_class;
};

struct sysThrot_driver sysThrot_dev = {
    .fops = fops,
    .active_calls = 0,
    .bitmap_size=__NR_syscalls/8+1,
};









int device_driver_init(void){
    dev_t dev;
    int result;
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
    cdev_del(&sysThrot_dev.cdev);
    device_destroy(sysThrot_dev.device_class, MKDEV(major_number, minor_number));
    class_destroy(sysThrot_dev.device_class);
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
    char *user_id_ptr :
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
    return ret;
}
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_name){
       char *user_id_ptr :
    int res=strncpy_from_user(program_name, (char __user *)program_name, TASK_COMM_LEN);
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
    return ret;
}


inline int check_if_registered(void){
    struct task_struct *task = current;
    if (find_program_in_store(sysThrot_dev.store, task->comm)) {
        AUDIT
        printk("%s: Intercepted syscall from process %s (PID %d)\n", MODNAME, task->comm, task->pid);
        return  1;
    }else if (find_user_in_store(sysThrot_dev.store, &(current_uid().val))){
        AUDIT
        printk("%s: Intercepted syscall from user with ID %d\n", MODNAME, current_uid().val);
        return 1;
    }
    return -1;
}

void temp(struct pt_regs *regs) {
    
    
    if (check_if_registered() == 1) {
        msleep(3000);
    }
}




asm(
".global stub_trampoline\n"
"stub_trampoline:\n"
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
extern void stub_trampoline(void);


int installProbe(int syscall_id){
    struct kprobe kp;
    unsigned long addr_sys;
    if (sysThrot_dev.syscall_addresses[syscall_id] != 0) 
        goto alredy_probed;
    AUDIT
    printk("%s: Installing kprobe for syscall (ID %d)\n", MODNAME, syscall_id);

    if (syscall_id < 0 || syscall_id >= ARRAY_SIZE(syscall_symbols))
        return -EINVAL;

    kp.symbol_name = syscall_symbols[syscall_id];
   
    if (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8))) {
        AUDIT
        printk("%s: Syscall (ID %d) is already registered\n", MODNAME, syscall_id);
        return -EEXIST;
    }

    int result = register_kprobe(&kp);
    if (result) {
        printk("%s: Failed to register kprobe for syscall (ID %d), with error %d\n", MODNAME, syscall_id,result);
        return result;
    }  
    //sarebbe da sistemare per poi metterlo apposto
    addr_sys = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    kp.addr= 0;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    AUDIT
    printk("%s: Probed syscall (ID %d) %s  at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);

    goto end;
    
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
        AUDIT
        printk("%s: Syscall (ID %d) %s is already probed at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);
    end:
        int INTS_LEN=5;
        char call_instruction[INTS_LEN];
        call_instruction[0]=0xE8; // opcode for CALL rel32
        int offset = (unsigned long)stub_trampoline - addr_sys - INTS_LEN;
        memcpy(call_instruction + 1, &offset, sizeof(int));
        unsigned long cr0, cr4;
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, INTS_LEN);
        end_sys_call_hacking(cr0, cr4);
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] |= (1 << (syscall_id % 8));
        return 1;
}



int removeProbe(int syscall_id){
    struct kprobe kp;
    unsigned long addr_sys;
    
    if (sysThrot_dev.syscall_addresses[syscall_id] !=  0)
        goto alredy_probed;
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
    addr_sys = (unsigned long)kp.addr;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    unregister_kprobe(&kp);
    AUDIT
    printk("%s: Probed syscall (ID %d) %s  at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);

    goto end;
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
        AUDIT
        printk("%s: Syscall (ID %d) %s is already probed at address 0x%lx\n", MODNAME, syscall_id,syscall_symbols[syscall_id], addr_sys);
    end:
        kp.addr= 0;
        int INTS_LEN=5;
        char call_instruction[INTS_LEN];
        call_instruction[0]=0x0f; // putting multi NOP, putting 5 normale nops may trigger ftrace exceptions.
        call_instruction[1]=0x1f;
        call_instruction[2]=0x44;
        call_instruction[3]=0x00;
        call_instruction[4]=0x00;
        unsigned long cr0, cr4;
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, INTS_LEN);
        end_sys_call_hacking(cr0, cr4);
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] &= ~(1 << (syscall_id % 8));
        return 1;
}   
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    
    return installProbe(syscall_id);
}
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id){
    return removeProbe(syscall_id);
}









int sysThrot_init(void) {
    int ret;
    sysThrot_dev.max_calls = max_calls_monitor;
    printk("%s: initializing\n",MODNAME);
    printk("%d concurrent calls allowed\n", max_calls_monitor);
    sysThrot_driver_init_store(&sysThrot_dev.store);
    ret = device_driver_init();
    if (ret < 0) {
        printk("%s: device driver init failed\n",MODNAME);
        return ret;
    }

    printk("%s: module correctly mounted\n",MODNAME);
    return 0;
}


void sysThrot_cleanup(void) {
    sysThrot_driver_cleanup_store(&sysThrot_dev.store);
    device_driver_cleanup();

    printk("%s: shutting down\n",MODNAME);
}

static const char *syscall_symbols[333] = {
    "__x64_sys_read",
    "__x64_sys_write",
    "__x64_sys_open",
    "__x64_sys_close",
    "__x64_sys_stat",
    "__x64_sys_fstat",
    "__x64_sys_lstat",
    "__x64_sys_poll",
    "__x64_sys_lseek",
    "__x64_sys_mmap",
    "__x64_sys_mprotect",
    "__x64_sys_munmap",
    "__x64_sys_brk",
    "__x64_sys_rt_sigaction",
    "__x64_sys_rt_sigprocmask",
    "__x64_sys_rt_sigreturn",
    "__x64_sys_ioctl",
    "__x64_sys_pread64",
    "__x64_sys_pwrite64",
    "__x64_sys_readv",
    "__x64_sys_writev",
    "__x64_sys_access",
    "__x64_sys_pipe",
    "__x64_sys_select",
    "__x64_sys_sched_yield",
    "__x64_sys_mremap",
    "__x64_sys_msync",
    "__x64_sys_mincore",
    "__x64_sys_madvise",
    "__x64_sys_shmget",
    "__x64_sys_shmat",
    "__x64_sys_shmctl",
    "__x64_sys_dup",
    "__x64_sys_dup2",
    "__x64_sys_pause",
    "__x64_sys_nanosleep",
    "__x64_sys_getitimer",
    "__x64_sys_alarm",
    "__x64_sys_setitimer",
    "__x64_sys_getpid",
    "__x64_sys_sendfile",
    "__x64_sys_socket",
    "__x64_sys_connect",
    "__x64_sys_accept",
    "__x64_sys_sendto",
    "__x64_sys_recvfrom",
    "__x64_sys_sendmsg",
    "__x64_sys_recvmsg",
    "__x64_sys_shutdown",
    "__x64_sys_bind",
    "__x64_sys_listen",
    "__x64_sys_getsockname",
    "__x64_sys_getpeername",
    "__x64_sys_socketpair",
    "__x64_sys_setsockopt",
    "__x64_sys_getsockopt",
    "__x64_sys_clone",
    "__x64_sys_fork",
    "__x64_sys_vfork",
    "__x64_sys_execve",
    "__x64_sys_exit",
    "__x64_sys_wait4",
    "__x64_sys_kill",
    "__x64_sys_uname",
    "__x64_sys_semget",
    "__x64_sys_semop",
    "__x64_sys_semctl",
    "__x64_sys_shmdt",
    "__x64_sys_msgget",
    "__x64_sys_msgsnd",
    "__x64_sys_msgrcv",
    "__x64_sys_msgctl",
    "__x64_sys_fcntl",
    "__x64_sys_flock",
    "__x64_sys_fsync",
    "__x64_sys_fdatasync",
    "__x64_sys_truncate",
    "__x64_sys_ftruncate",
    "__x64_sys_getdents",
    "__x64_sys_getcwd",
    "__x64_sys_chdir",
    "__x64_sys_fchdir",
    "__x64_sys_rename",
    "__x64_sys_mkdir",
    "__x64_sys_rmdir",
    "__x64_sys_creat",
    "__x64_sys_link",
    "__x64_sys_unlink",
    "__x64_sys_symlink",
    "__x64_sys_readlink",
    "__x64_sys_chmod",
    "__x64_sys_fchmod",
    "__x64_sys_chown",
    "__x64_sys_fchown",
    "__x64_sys_lchown",
    "__x64_sys_umask",
    "__x64_sys_gettimeofday",
    "__x64_sys_getrlimit",
    "__x64_sys_getrusage",
    "__x64_sys_sysinfo",
    "__x64_sys_times",
    "__x64_sys_ptrace",
    "__x64_sys_getuid",
    "__x64_sys_syslog",
    "__x64_sys_getgid",
    "__x64_sys_setuid",
    "__x64_sys_setgid",
    "__x64_sys_geteuid",
    "__x64_sys_getegid",
    "__x64_sys_setpgid",
    "__x64_sys_getppid",
    "__x64_sys_getpgrp",
    "__x64_sys_setsid",
    "__x64_sys_setreuid",
    "__x64_sys_setregid",
    "__x64_sys_getgroups",
    "__x64_sys_setgroups",
    "__x64_sys_setresuid",
    "__x64_sys_getresuid",
    "__x64_sys_setresgid",
    "__x64_sys_getresgid",
    "__x64_sys_getpgid",
    "__x64_sys_setfsuid",
    "__x64_sys_setfsgid",
    "__x64_sys_getsid",
    "__x64_sys_capget",
    "__x64_sys_capset",
    "__x64_sys_rt_sigpending",
    "__x64_sys_rt_sigtimedwait",
    "__x64_sys_rt_sigqueueinfo",
    "__x64_sys_rt_sigsuspend",
    "__x64_sys_sigaltstack",
    "__x64_sys_utime",
    "__x64_sys_mknod",
    "__x64_sys_uselib",
    "__x64_sys_personality",
    "__x64_sys_ustat",
    "__x64_sys_statfs",
    "__x64_sys_fstatfs",
    "__x64_sys_sysfs",
    "__x64_sys_getpriority",
    "__x64_sys_setpriority",
    "__x64_sys_sched_setparam",
    "__x64_sys_sched_getparam",
    "__x64_sys_sched_setscheduler",
    "__x64_sys_sched_getscheduler",
    "__x64_sys_sched_get_priority_max",
    "__x64_sys_sched_get_priority_min",
    "__x64_sys_sched_rr_get_interval",
    "__x64_sys_mlock",
    "__x64_sys_munlock",
    "__x64_sys_mlockall",
    "__x64_sys_munlockall",
    "__x64_sys_vhangup",
    "__x64_sys_modify_ldt",
    "__x64_sys_pivot_root",
    "__x64_sys__sysctl",
    "__x64_sys_prctl",
    "__x64_sys_arch_prctl",
    "__x64_sys_adjtimex",
    "__x64_sys_setrlimit",
    "__x64_sys_chroot",
    "__x64_sys_sync",
    "__x64_sys_acct",
    "__x64_sys_settimeofday",
    "__x64_sys_mount",
    "__x64_sys_umount2",
    "__x64_sys_swapon",
    "__x64_sys_swapoff",
    "__x64_sys_reboot",
    "__x64_sys_sethostname",
    "__x64_sys_setdomainname",
    "__x64_sys_iopl",
    "__x64_sys_ioperm",
    "__x64_sys_create_module",
    "__x64_sys_init_module",
    "__x64_sys_delete_module",
    "__x64_sys_get_kernel_syms",
    "__x64_sys_query_module",
    "__x64_sys_quotactl",
    "__x64_sys_nfsservctl",
    "__x64_sys_getpmsg",
    "__x64_sys_putpmsg",
    "__x64_sys_afs_syscall",
    "__x64_sys_tuxcall",
    "__x64_sys_security",
    "__x64_sys_gettid",
    "__x64_sys_readahead",
    "__x64_sys_setxattr",
    "__x64_sys_lsetxattr",
    "__x64_sys_fsetxattr",
    "__x64_sys_getxattr",
    "__x64_sys_lgetxattr",
    "__x64_sys_fgetxattr",
    "__x64_sys_listxattr",
    "__x64_sys_llistxattr",
    "__x64_sys_flistxattr",
    "__x64_sys_removexattr",
    "__x64_sys_lremovexattr",
    "__x64_sys_fremovexattr",
    "__x64_sys_tkill",
    "__x64_sys_time",
    "__x64_sys_futex",
    "__x64_sys_sched_setaffinity",
    "__x64_sys_sched_getaffinity",
    "__x64_sys_set_thread_area",
    "__x64_sys_io_setup",
    "__x64_sys_io_destroy",
    "__x64_sys_io_getevents",
    "__x64_sys_io_submit",
    "__x64_sys_io_cancel",
    "__x64_sys_get_thread_area",
    "__x64_sys_lookup_dcookie",
    "__x64_sys_epoll_create",
    "__x64_sys_epoll_ctl_old",
    "__x64_sys_epoll_wait_old",
    "__x64_sys_remap_file_pages",
    "__x64_sys_getdents64",
    "__x64_sys_set_tid_address",
    "__x64_sys_restart_syscall",
    "__x64_sys_semtimedop",
    "__x64_sys_fadvise64",
    "__x64_sys_timer_create",
    "__x64_sys_timer_settime",
    "__x64_sys_timer_gettime",
    "__x64_sys_timer_getoverrun",
    "__x64_sys_timer_delete",
    "__x64_sys_clock_settime",
    "__x64_sys_clock_gettime",
    "__x64_sys_clock_getres",
    "__x64_sys_clock_nanosleep",
    "__x64_sys_exit_group",
    "__x64_sys_epoll_wait",
    "__x64_sys_epoll_ctl",
    "__x64_sys_tgkill",
    "__x64_sys_utimes",
    "__x64_sys_vserver",
    "__x64_sys_mbind",
    "__x64_sys_set_mempolicy",
    "__x64_sys_get_mempolicy",
    "__x64_sys_mq_open",
    "__x64_sys_mq_unlink",
    "__x64_sys_mq_timedsend",
    "__x64_sys_mq_timedreceive",
    "__x64_sys_mq_notify",
    "__x64_sys_mq_getsetattr",
    "__x64_sys_kexec_load",
    "__x64_sys_waitid",
    "__x64_sys_add_key",
    "__x64_sys_request_key",
    "__x64_sys_keyctl",
    "__x64_sys_ioprio_set",
    "__x64_sys_ioprio_get",
    "__x64_sys_inotify_init",
    "__x64_sys_inotify_add_watch",
    "__x64_sys_inotify_rm_watch",
    "__x64_sys_migrate_pages",
    "__x64_sys_openat",
    "__x64_sys_mkdirat",
    "__x64_sys_mknodat",
    "__x64_sys_fchownat",
    "__x64_sys_futimesat",
    "__x64_sys_newfstatat",
    "__x64_sys_unlinkat",
    "__x64_sys_renameat",
    "__x64_sys_linkat",
    "__x64_sys_symlinkat",
    "__x64_sys_readlinkat",
    "__x64_sys_fchmodat",
    "__x64_sys_faccessat",
    "__x64_sys_pselect6",
    "__x64_sys_ppoll",
    "__x64_sys_unshare",
    "__x64_sys_set_robust_list",
    "__x64_sys_get_robust_list",
    "__x64_sys_splice",
    "__x64_sys_tee",
    "__x64_sys_sync_file_range",
    "__x64_sys_vmsplice",
    "__x64_sys_move_pages",
    "__x64_sys_utimensat",
    "__x64_sys_epoll_pwait",
    "__x64_sys_signalfd",
    "__x64_sys_timerfd_create",
    "__x64_sys_eventfd",
    "__x64_sys_fallocate",
    "__x64_sys_timerfd_settime",
    "__x64_sys_timerfd_gettime",
    "__x64_sys_accept4",
    "__x64_sys_signalfd4",
    "__x64_sys_eventfd2",
    "__x64_sys_epoll_create1",
    "__x64_sys_dup3",
    "__x64_sys_pipe2",
    "__x64_sys_inotify_init1",
    "__x64_sys_preadv",
    "__x64_sys_pwritev",
    "__x64_sys_rt_tgsigqueueinfo",
    "__x64_sys_perf_event_open",
    "__x64_sys_recvmmsg",
    "__x64_sys_fanotify_init",
    "__x64_sys_fanotify_mark",
    "__x64_sys_prlimit64",
    "__x64_sys_name_to_handle_at",
    "__x64_sys_open_by_handle_at",
    "__x64_sys_clock_adjtime",
    "__x64_sys_syncfs",
    "__x64_sys_sendmmsg",
    "__x64_sys_setns",
    "__x64_sys_getcpu",
    "__x64_sys_process_vm_readv",
    "__x64_sys_process_vm_writev",
    "__x64_sys_kcmp",
    "__x64_sys_finit_module",
    "__x64_sys_sched_setattr",
    "__x64_sys_sched_getattr",
    "__x64_sys_renameat2",
    "__x64_sys_seccomp",
    "__x64_sys_getrandom",
    "__x64_sys_memfd_create",
    "__x64_sys_kexec_file_load",
    "__x64_sys_bpf",
    "__x64_sys_execveat",
    "__x64_sys_userfaultfd",
    "__x64_sys_membarrier",
    "__x64_sys_mlock2",
    "__x64_sys_copy_file_range",
    "__x64_sys_preadv2",
    "__x64_sys_pwritev2",
    "__x64_sys_pkey_mprotect",
    "__x64_sys_pkey_alloc",
    "__x64_sys_pkey_free",
    "__x64_sys_statx"
};




module_init(sysThrot_init);
module_exit(sysThrot_cleanup);
