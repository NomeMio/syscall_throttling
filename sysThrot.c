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











MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Syscall Throttling Module");

DECLARE_WAIT_QUEUE_HEAD(wait_q);

int major_number;
int minor_number=0;

unsigned long max_calls_monitor = 0;
module_param(max_calls_monitor, ulong, 0660);

int device_driver_init(void);
int device_driver_cleanup(void);




void timer_callback(struct timer_list *timer);
inline int check_if_registered(void);
void stub(struct pt_regs *regs);
int installProbe(int syscall_id);
int removeProbe(int syscall_id);
struct users_list_array* get_user_space_users_copy(void);
int sysThrot_init(void);
void sysThrot_cleanup(void);

extern void stub_trampoline(void);

static const char *syscall_symbols[];



//Device driver stuff

const struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = sysThrot_ioctl,
};
struct sysThrot_driver sysThrot_dev = {
    .fops = fops,
    .bitmap_size=__NR_syscalls/8+1,
};



inline int check_if_registered(void){
    struct task_struct *task = current;
    if (find_program_in_store(sysThrot_dev.store, task->comm) ==  0) {
        AUDIT
        SYS_AUDIT_LOG("Intercepted syscall from process %s (PID %d)", task->comm, task_pid_nr(task));
        return 1;
    } else {
        int uid = __kuid_val(current_uid());

        if (find_user_in_store(sysThrot_dev.store, &uid) == 0) {
            AUDIT
            SYS_AUDIT_LOG("Intercepted syscall from user with ID %u", uid);
            return 1;
        }
    }
    return -1;
}




void stub(struct pt_regs *regs) {
    atomic_inc(&sysThrot_dev.critical.threads_in_stub);
    if(sysThrot_dev.working==0) {
       goto global_end;
    }
    atomic_inc(&sysThrot_dev.critical.threads_in_module);
    if (check_if_registered() != 1) {
        goto end;
    }

    unsigned long flags;
    int wakeup_flag=0;
    //TODO: si potrebbe fare un meccanismo di sync/cons migliore
    spin_lock_irqsave(&sysThrot_dev.critical.queue_lock, flags);
    int tokens_left = atomic_dec_return(&sysThrot_dev.critical.current_epoch_tokens);
    if (tokens_left >= 0) {
            spin_unlock_irqrestore(&sysThrot_dev.critical.queue_lock, flags);
        goto end;
    }
    struct task_struct *task = current;
    struct _queue_elem *elem = kmalloc(sizeof(struct _queue_elem), GFP_KERNEL);
    if (!elem) {
        spin_unlock_irqrestore(&sysThrot_dev.critical.queue_lock, flags);
        goto end;
    }
    elem->task = task;
    elem->to_wake = &wakeup_flag;
    elem->was_interrupted = 0;
    elem->next = NULL;
    sysThrot_dev.critical.sentinel_tail.next->next = elem;
    sysThrot_dev.critical.sentinel_tail.next = elem;
    spin_unlock_irqrestore(&sysThrot_dev.critical.queue_lock, flags);   
    int res=wait_event_interruptible(wait_q, wakeup_flag == 1 || sysThrot_dev.working == 0);
    if (res == -ERESTARTSYS) {
        spin_lock_irqsave(&sysThrot_dev.critical.interrupt_lock, flags);
        elem->was_interrupted = 1;
        spin_unlock_irqrestore(&sysThrot_dev.critical.interrupt_lock, flags);
        SYS_AUDIT_LOG("wait interrupted by signal for process %s (PID %d)", task->comm, task_pid_nr(task));
    }
    end:
    atomic_dec(&sysThrot_dev.critical.threads_in_module);
    global_end:
    atomic_dec(&sysThrot_dev.critical.threads_in_stub);
    return;
    
}




asm(
".global stub_trampoline\n"
"stub_trampoline:\n"
"    pushq %rax\n" 
"    pushq %rdi\n"
"    pushq %rsi\n"
"    pushq %rdx\n"
"    pushq %rcx\n"
"    pushq %r11\n"
"    pushq %r10\n"
"    pushq %r9\n"
"    pushq %r8\n"
"    call stub\n"  
"    popq %r8\n"  
"    popq %r9\n"
"    popq %r10\n"
"    popq %r11\n"
"    popq %rcx\n"
"    popq %rdx\n"
"    popq %rsi\n"
"    popq %rdi\n"
"    popq %rax\n"
"    ret\n"        
);
extern void stub_trampoline(void);


int installProbe(int syscall_id){
    struct kprobe kp;
    unsigned long addr_sys;
    if (sysThrot_dev.syscall_addresses[syscall_id] != 0) 
        goto alredy_probed;
    //SYS_AUDIT_LOG("Installing kprobe for syscall (ID %d) registration", syscall_id);
    if (syscall_id < 0 || syscall_id >= SUPPORTED_SYSCALLS)
        return -EINVAL;

    kp.symbol_name = syscall_symbols[syscall_id];
   
    if (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8))) {
        //SYS_AUDIT_LOG("Syscall (ID %d: %s) is already registered", syscall_id, syscall_symbols[syscall_id]);
        return -EEXIST;
    }

    int result = register_kprobe(&kp);
    if (result) {
        SYS_AUDIT_LOG("%s: Failed to register kprobe for syscall (ID %d: %s) registration, with error %d\n", MODNAME, syscall_id, syscall_symbols[syscall_id], result);
        return result;
    }  
    //sarebbe da sistemare per poi metterlo apposto
    addr_sys = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    kp.addr= 0;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    AUDIT
    SYS_AUDIT_LOG("Probed syscall (ID %d: %s) at address 0x%lx for registration", syscall_id, syscall_symbols[syscall_id], addr_sys);

    goto end;
    
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
        AUDIT
        SYS_AUDIT_LOG("Syscall (ID %d: %s) is already probed at address 0x%lx for registration", syscall_id, syscall_symbols[syscall_id], addr_sys);
    end:
        int INTS_LEN=5;
        char call_instruction[INTS_LEN];
        call_instruction[0]=0xE8; // opcode for CALL rel32
        int offset = (unsigned long)stub_trampoline - addr_sys - INTS_LEN;
        memcpy(call_instruction + 1, &offset, sizeof(int));
        unsigned long cr0, cr4;
        preempt_disable();
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, INTS_LEN);
        end_sys_call_hacking(cr0, cr4);
        preempt_enable();
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] |= (1 << (syscall_id % 8));
        return 1;
}



int removeProbe(int syscall_id){
    struct kprobe kp;
    unsigned long addr_sys;
    
    if (sysThrot_dev.syscall_addresses[syscall_id] !=  0)
        goto alredy_probed;
    if (syscall_id < 0 || syscall_id >= SUPPORTED_SYSCALLS)
        return -EINVAL;
    if( (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8)))==0 )
            return -EINVAL; 
    kp.symbol_name = syscall_symbols[syscall_id];
    int result = register_kprobe(&kp);
    if (result) {
        SYS_AUDIT_LOG("%s: Failed to register kprobe for syscall (ID %d: %s) deregistration, with error %d\n", MODNAME, syscall_id, syscall_symbols[syscall_id], result);
        return -1;
    }   
    //sarebbe da sistemare per poi metterlo apposto
    addr_sys = (unsigned long)kp.addr;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    unregister_kprobe(&kp);
    SYS_AUDIT_LOG("Probed syscall (ID %d: %s) at address 0x%lx for deregistration", syscall_id, syscall_symbols[syscall_id], addr_sys);

    goto end;
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
    end:
        kp.addr= 0;
        int INTS_LEN=5;
        
        char call_instruction[INTS_LEN];
        
        call_instruction[0]=0x0f; // putting multi NOP, putting 5 normale nops sometimes trigger some strange error's with ftrace, TODO check normal NOP.
        call_instruction[1]=0x1f;
        call_instruction[2]=0x44;
        call_instruction[3]=0x00;
        call_instruction[4]=0x00;
        
        unsigned long cr0, cr4;
        preempt_disable();
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, INTS_LEN);
        end_sys_call_hacking(cr0, cr4);
        preempt_enable();
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] &= ~(1 << (syscall_id % 8));
        return 1;
}   




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

 
void timer_callback(struct timer_list *timer){
        int new_epoche_tokens=sysThrot_dev.max_syscalls_for_epoch;
        unsigned long flags;
        spin_lock_irqsave(&sysThrot_dev.critical.queue_lock, flags);
        struct _queue_elem *elem = sysThrot_dev.critical.sentinel_head.next;
        
        wake_up_loop:
            if (elem == NULL) {
                sysThrot_dev.critical.sentinel_tail.next = &sysThrot_dev.critical.sentinel_head;
                goto end_wake_up_loop;
            }
            if (new_epoche_tokens <= 0) {
                goto end_wake_up_loop;
            }
            long flags_interrupt;
            spin_lock_irqsave(&sysThrot_dev.critical.interrupt_lock, flags_interrupt);
            if(elem->was_interrupted==0)
                *(elem->to_wake) = 1;
            spin_unlock_irqrestore(&sysThrot_dev.critical.interrupt_lock, flags_interrupt);
            struct _queue_elem *to_free = elem;
            elem = elem->next;
            kfree(to_free);
            sysThrot_dev.critical.sentinel_head.next = elem;
            new_epoche_tokens--;
            goto wake_up_loop;
        end_wake_up_loop:
            wake_up_interruptible(&wait_q);
            atomic_inc(&sysThrot_dev.critical.epoch);
            atomic_set(&sysThrot_dev.critical.current_epoch_tokens, new_epoche_tokens);
            spin_unlock_irqrestore(&sysThrot_dev.critical.queue_lock, flags);
        if(sysThrot_dev.working) mod_timer(&sysThrot_dev.timer, jiffies + msecs_to_jiffies(EPOCH_DURATION_MS));
}


int sysThrot_init(void) {
    printk("%s: initializing module\n", MODNAME);
    int ret;
    ret = sysThrot_log_init();
    if (ret < 0) {
        printk(KERN_ALERT "%s: failed to initialize pseudo log\n", MODNAME);
        return ret;
    }
    SYS_AUDIT_LOG("initializing");
    SYS_AUDIT_LOG("%ld concurrent calls allowed", max_calls_monitor);
    init_sysThrot_store(&sysThrot_dev.store);
    ret = device_driver_init();
    if (ret < 0) {
        printk("%s: device driver init failed\n",MODNAME);
        sysThrot_log_cleanup();
        return ret;
    }
    sysThrot_dev.working=0;
    sysThrot_dev.max_syscalls_for_epoch=max_calls_monitor;
    spin_lock_init(&sysThrot_dev.critical.queue_lock);
    spin_lock_init(&sysThrot_dev.critical.interrupt_lock);
    atomic_set(&sysThrot_dev.critical.threads_in_stub, 0);
    atomic_set(&sysThrot_dev.critical.epoch, 0);
    atomic_set(&sysThrot_dev.critical.current_epoch_tokens, 0);
    ret= sysThrot_turn_on();
    if (ret < 0) {
        device_driver_cleanup();
        printk("%s: failed to turn on throttling\n",MODNAME);
        sysThrot_log_cleanup();
        return ret;
    }
    printk("%s: module loaded\n", MODNAME);
    SYS_AUDIT_LOG("module correctly mounted");
    return 0;
}

int sysThrot_turn_on(void){
    int ret;
    if(sysThrot_dev.working==1)
        return -EALREADY; //non so cos altro usare
    sysThrot_dev.critical.sentinel_head.task = NULL;
    sysThrot_dev.critical.sentinel_head.to_wake = NULL;
    sysThrot_dev.critical.sentinel_head.was_interrupted=0;
    sysThrot_dev.critical.sentinel_head.next = NULL;
    sysThrot_dev.critical.sentinel_tail.task = NULL;
    sysThrot_dev.critical.sentinel_tail.to_wake = NULL;
    sysThrot_dev.critical.sentinel_tail.was_interrupted=0;
    sysThrot_dev.critical.sentinel_tail.next = &sysThrot_dev.critical.sentinel_head;
    timer_setup(&sysThrot_dev.timer, timer_callback, 0);
    ret=mod_timer(&sysThrot_dev.timer, jiffies + msecs_to_jiffies(EPOCH_DURATION_MS));
    if (ret) {
        printk("%s: Error in mod_timer\n", MODNAME);
        return ret;
    }
    sysThrot_dev.working=1;
    SYS_AUDIT_LOG("Throttling turned ON");
    return 0;
}


int sysThrot_turn_off(void){
    if(sysThrot_dev.working==0)
        return -EALREADY; //non so cos altro usare#
    timer_shutdown_sync(&sysThrot_dev.timer);   
    sysThrot_dev.working=0; 
    while(atomic_read(&sysThrot_dev.critical.threads_in_module)>0){
            SYS_AUDIT_LOG("Waiting for %d threads to exit the module before turning off throttling", atomic_read(&sysThrot_dev.critical.threads_in_module));
            wake_up_interruptible(&wait_q);
            msleep(100);

        }
    sysThrot_dev.critical.sentinel_head.task = NULL;
    sysThrot_dev.critical.sentinel_head.to_wake = 0;
    sysThrot_dev.critical.sentinel_head.next = NULL;
    sysThrot_dev.critical.sentinel_tail.task = NULL;
    sysThrot_dev.critical.sentinel_tail.to_wake = 0;
    sysThrot_dev.critical.sentinel_tail.next = &sysThrot_dev.critical.sentinel_head;
    SYS_AUDIT_LOG("Throttling turned OFF");
    return 0;
}

void sysThrot_cleanup(void) {
    sysThrot_turn_off();
    int left;
    do{
        left=atomic_read(&sysThrot_dev.critical.threads_in_stub);
        if(left>0){
            SYS_AUDIT_LOG("Waiting for %d threads to exit the stub before cleanup", left);
            msleep(100); 
        }
    }while(left>0);
    for(int i=0;i<__NR_syscalls/8+1;i++){
        if(sysThrot_dev.syscall_presence_bitmap[i]!=0){
            for(int j=0;j<8;j++){
                int syscall_id=i*8+j;
                if(sysThrot_dev.syscall_presence_bitmap[i] & (1 << j)){
                    removeProbe(syscall_id);
                }
            }
        }
    }
    destroy_sysThrot_store(sysThrot_dev.store);
    device_driver_cleanup();
    SYS_AUDIT_LOG("shutting down");
    sysThrot_log_cleanup();
    printk("%s: module unloaded\n", MODNAME);
}


struct users_list_array* get_user_space_users_copy(){
    return get_user_space_users_array_from_store(sysThrot_dev.store);
}





module_init(sysThrot_init);
module_exit(sysThrot_cleanup);


