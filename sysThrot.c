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
#include "sysThrot.h"
#include "sysThrot_store.h"
#include "sysThrot_ioctl.h"
#include "sysThrot_queue.h"
#include "sysThrot_statmonitor.h"










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
int sysThrot_init(void);
void sysThrot_cleanup(void);


void timer_callback(struct timer_list *timer);
inline int check_if_registered(void);
int stub(struct pt_regs *regs);
int installProbe(int syscall_id);
int removeProbe(int syscall_id);



extern void stub_trampoline(void);


static atomic_t dev_available = ATOMIC_INIT(1); //For now only one user at time can open the device, but it should be enough for our use case, if needed it can be easily changed to allow more concurrent users
#define MAX_STORE_USERS    256
static DEFINE_MUTEX(text_patch_lock);

static ssize_t sysThrot_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
    char *buf;
    int  *users     = NULL;
    char **programs = NULL;
    int   num_users = 0, num_programs = 0;
    int   len = 0;
    const int buf_size = PAGE_SIZE * 4;
    ssize_t ret;

    if (__kuid_val(current_uid()) != 0)
        return -EPERM;

    buf = kmalloc(buf_size, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    users = kmalloc_array(MAX_STORE_USERS, sizeof(int), GFP_KERNEL);
    if (!users) { ret = -ENOMEM; goto out; }

    programs = kcalloc(MAX_STORE_PROGRAMS, sizeof(char *), GFP_KERNEL);
    if (!programs) { ret = -ENOMEM; goto out; }
    for (int i = 0; i < MAX_STORE_PROGRAMS; i++) {
        programs[i] = kmalloc(TASK_COMM_LEN, GFP_KERNEL);
        if (!programs[i]) { ret = -ENOMEM; goto out; }
    }

    scnprintf(buf + len, buf_size - len, "=== Syscall Throttling Module Status ===\n");
    len += scnprintf(buf + len, buf_size - len, "Throttling is %s\n", sysThrot_dev.working ? "ON" : "OFF");
    len += scnprintf(buf + len, buf_size - len, "Max syscalls per epoch: %d\n", sysThrot_dev.max_syscalls_for_epoch);


    get_all_users_from_store(sysThrot_dev.store, users, &num_users);
    len += scnprintf(buf + len, buf_size - len, "=== Registered Users (%d) ===\n", num_users);
    for (int i = 0; i < num_users; i++)
        len += scnprintf(buf + len, buf_size - len, "  UID: %d\n", users[i]);

    get_all_programs_from_store(sysThrot_dev.store, programs, &num_programs);
    len += scnprintf(buf + len, buf_size - len, "\n=== Registered Programs (%d) ===\n", num_programs);
    for (int i = 0; i < num_programs; i++)
        len += scnprintf(buf + len, buf_size - len, "  %s\n", programs[i]);

    len += scnprintf(buf + len, buf_size - len, "\n=== Monitored Syscalls ===\n");
    for (int i = 0; i < SUPPORTED_SYSCALLS; i++) {
        if ((sysThrot_dev.syscall_presence_bitmap[i / 8] & (1 << (i % 8))) && syscall_symbols[i])
            len += scnprintf(buf + len, buf_size - len, "  [%d] %s\n", i, syscall_symbols[i]);
    }

    ret = simple_read_from_buffer(ubuf, count, ppos, buf, len);

out:
    if (programs) {
        for (int i = 0; i < MAX_STORE_PROGRAMS; i++)
            kfree(programs[i]);
        kfree(programs);
    }
    kfree(users);
    kfree(buf);
    return ret;
}

int sysThrot_open(struct inode *inode, struct file *flip){
    //struct sysThrot_driver *dev = &sysThrot_dev; 
    if (! atomic_dec_and_test (&dev_available)) {
    atomic_inc(&dev_available);
    return -EBUSY; /* already open */
    }
    return 0;
}

int sysThrot_release(struct inode *inode, struct file *flip){
    atomic_inc(&dev_available);
    return 0;
}


//Device driver stuff


const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .read           = sysThrot_read,
    .unlocked_ioctl = sysThrot_ioctl,
    .open           = sysThrot_open,
    .release        = sysThrot_release,
};
struct sysThrot_driver sysThrot_dev = {
    .fops = fops,
    .bitmap_size=__NR_syscalls/8+1,
};



inline int check_if_registered(void){
    struct task_struct *task = current;
    if (find_program_in_store(sysThrot_dev.store, task->comm) ==  0) {
        LOG(LOG_CHECK_IF_LIMITED,"Intercepted syscall from process %s (PID %d)", task->comm, task_pid_nr(task));
        return 1;
    } else {
        int uid = __kuid_val(current_uid());
        if (find_user_in_store(sysThrot_dev.store, &uid) == 0) {
            LOG(LOG_CHECK_IF_LIMITED,"Intercepted syscall from user with ID %u", uid);
            return 1;
        }
    }
    return -1;
}




/*  
    threads stub is needed to decide if the module is desmountable
    thread in module is needed to turn off compltely the module when asked, they are separated to avoid the  need to unook the syscall each time
*/

/*
    Stats could be handled in a better way, but for now i dont care, ask professor if needed, performance and concurrency is not a priority.
*/
int stub(struct pt_regs *regs) {
    atomic_inc(&sysThrot_dev.critical.threads_in_stub);
    if(sysThrot_dev.working==0) {
       goto global_end;
    }
    atomic_inc(&sysThrot_dev.critical.threads_in_module);
    if (check_if_registered() != 1) {
        goto end;
    }


    int tokens_left = atomic_dec_return(&sysThrot_dev.critical.current_epoch_tokens);
    if (tokens_left >= 0) {
        goto end;
    }
    ktime_t clock_in = ktime_to_ns(ktime_get());

    int *wakeup_flag = add_to_queue();

    int result;
    if (wakeup_flag) {
        result = wait_event_interruptible(wait_q,
            ({ sysThrot_add_ctx_switch(regs->orig_ax); *wakeup_flag == 1 || sysThrot_dev.working == 0; }));
        set_exited_flag(wakeup_flag);
    } else {
        wait_event(wait_q,
            ({ sysThrot_add_ctx_switch(regs->orig_ax);
               atomic_read(&sysThrot_dev.critical.current_epoch_tokens) > 0 || sysThrot_dev.working == 0; }));
        if (sysThrot_dev.working)
            atomic_dec(&sysThrot_dev.critical.current_epoch_tokens);
        result = 0;
    }
    if (result == -ERESTARTSYS) {
        LOG(LOG_STUB,"Thread interrupted by signal while waiting in queue, failing the syscall");
        regs->ax = -EAGAIN;
        atomic_dec(&sysThrot_dev.critical.threads_in_module);
        atomic_dec(&sysThrot_dev.critical.threads_in_stub);
        return -EAGAIN;
    }
    ktime_t clock_out = ktime_to_ns(ktime_get());
    unsigned long delay_ns = clock_out - clock_in;
    int syscall_id = regs->orig_ax;
    sysThrot_add_blocked(syscall_id, delay_ns);
    
    end:
    atomic_dec(&sysThrot_dev.critical.threads_in_module);
    global_end:
    atomic_dec(&sysThrot_dev.critical.threads_in_stub);
    return 0;
}



// its neceessary to save all registers that can get dirty, if not it crashes the whole kernel. To test it just eliminate the pushe and pop, and register read, it instantly corrupts
// if stub returns non-zero, the syscall body is skipped and the error is returned to userspace instead (rax + dropping the wrapper-body return address)
asm(
".global stub_trampoline\n"
"stub_trampoline:\n"
"    pushq %rbx\n"
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
"    movq %rax, %rbx\n"
"    popq %r8\n"
"    popq %r9\n"
"    popq %r10\n"
"    popq %r11\n"
"    popq %rcx\n"
"    popq %rdx\n"
"    popq %rsi\n"
"    popq %rdi\n"
"    popq %rax\n"
"    testq %rbx, %rbx\n"
"    jz 1f\n"
"    movq %rbx, %rax\n"
"    popq %rbx\n"
"    addq $8, %rsp\n"
"    ret\n"
"1:\n"
"    popq %rbx\n"
"    ret\n"
);

extern void stub_trampoline(void);


int installProbe(int syscall_id){
    if (syscall_id < 0 || syscall_id >= SUPPORTED_SYSCALLS)
        return -EINVAL;

    if (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8))) {
        LOG(LOG_CORE,"Syscall (ID %d: %s) is already probed for registration", syscall_id, syscall_symbols[syscall_id]);
        return -EEXIST;
    }

    if (sysThrot_dev.syscall_addresses[syscall_id] != 0) 
        goto alredy_probed;
    
    
    struct kprobe kp = {0};
    unsigned long addr_sys;
    kp.symbol_name = syscall_symbols[syscall_id];
    
    int result = register_kprobe(&kp);
    if (result) {
        LOG(LOG_CORE,"%s: Failed to register kprobe for syscall (ID %d: %s) registration, with error %d\n", MODNAME, syscall_id, syscall_symbols[syscall_id], result);
        return result;
    }  

    addr_sys = (unsigned long)kp.addr;
    unregister_kprobe(&kp);
    kp.addr= 0;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    LOG(LOG_CORE,"Probed syscall (ID %d: %s) at address 0x%lx for registration", syscall_id, syscall_symbols[syscall_id], addr_sys);

    goto end;
    
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
        LOG(LOG_CORE,"Syscall (ID %d: %s) is already probed at address 0x%lx for registration", syscall_id, syscall_symbols[syscall_id], addr_sys);
    end:
        char call_instruction[5] = {0};
        call_instruction[0]=0xE8; // opcode for CALL rel32
        int offset = (unsigned long)stub_trampoline - addr_sys - sizeof(call_instruction);
        memcpy(call_instruction + 1, &offset, sizeof(int));
        unsigned long cr0, cr4;
        mutex_lock(&text_patch_lock);
        preempt_disable();
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, sizeof(call_instruction));
        end_sys_call_hacking(cr0, cr4);
        preempt_enable();
        mutex_unlock(&text_patch_lock);
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] |= (1 << (syscall_id % 8));
        return 1;
}



int removeProbe(int syscall_id){
    if (syscall_id < 0 || syscall_id >= SUPPORTED_SYSCALLS)
        return -EINVAL; 
    if( (sysThrot_dev.syscall_presence_bitmap[syscall_id/8] & (1 << (syscall_id % 8)))==0 )
            return -EEXIST; // syscall is not probed for deregistration

        
    if (sysThrot_dev.syscall_addresses[syscall_id] !=  0)
        goto alredy_probed;

    struct kprobe kp = {0};
    unsigned long addr_sys;
    kp.symbol_name = syscall_symbols[syscall_id];
    int result = register_kprobe(&kp);
    if (result) {
        LOG(LOG_CORE,"%s: Failed to register kprobe for syscall (ID %d: %s) deregistration, with error %d\n", MODNAME, syscall_id, syscall_symbols[syscall_id], result);
        return -1;
    }   
    addr_sys = (unsigned long)kp.addr;
    sysThrot_dev.syscall_addresses[syscall_id]=addr_sys;
    unregister_kprobe(&kp);
    LOG(LOG_CORE,"Probed syscall (ID %d: %s) at address 0x%lx for deregistration", syscall_id, syscall_symbols[syscall_id], addr_sys);
    goto end;
    alredy_probed:
        addr_sys = sysThrot_dev.syscall_addresses[syscall_id];
    end:
        kp.addr= 0;
        char call_instruction[5] = {0};
        call_instruction[0]=0x0f; // putting multi NOP, putting 5 normale nops sometimes trigger some strange error's with ftrace, TODO check normal NOP.
        call_instruction[1]=0x1f;
        call_instruction[2]=0x44;
        call_instruction[3]=0x00;
        call_instruction[4]=0x00;
        unsigned long cr0, cr4;
        mutex_lock(&text_patch_lock);
        preempt_disable();
        being_sys_call_hacking(&cr0, &cr4);
        memcpy((void *)addr_sys, call_instruction, sizeof(call_instruction));
        end_sys_call_hacking(cr0, cr4);
        preempt_enable();
        mutex_unlock(&text_patch_lock);
        sysThrot_dev.syscall_presence_bitmap[syscall_id/8] &= ~(1 << (syscall_id % 8));
        return 1;
}   




int device_driver_init(void){
    dev_t dev;
    int result;
    result=alloc_chrdev_region(&dev, minor_number, 1, DEVICE_NAME);
    major_number=MAJOR(dev);
    if (result < 0) {
        LOG(LOG_CORE,"%s: Failed to allocate a major number\n", MODNAME);
        return result;
    }
    cdev_init(&sysThrot_dev.cdev, &sysThrot_dev.fops);
    sysThrot_dev.cdev.owner = THIS_MODULE;
    result = cdev_add(&sysThrot_dev.cdev, MKDEV(major_number, minor_number), 1);
    if (result < 0) {
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        LOG(LOG_CORE,"%s: Failed to add cdev\n", MODNAME);
        return result;
    }
    sysThrot_dev.device_class=class_create(DEVICE_NAME);
    if(IS_ERR(sysThrot_dev.device_class)){
        cdev_del(&sysThrot_dev.cdev);
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        LOG(LOG_CORE,"%s: Failed to create device class\n", MODNAME);
        return PTR_ERR(sysThrot_dev.device_class);
    }
    if(IS_ERR(device_create(sysThrot_dev.device_class, NULL, MKDEV(major_number, minor_number), NULL, DEVICE_NAME))){
        class_destroy(sysThrot_dev.device_class);
        cdev_del(&sysThrot_dev.cdev);
        unregister_chrdev_region(MKDEV(major_number, minor_number), 1);
        LOG(LOG_CORE,"%s: Failed to create device\n", MODNAME);
        return -1;
    }
    return 0;
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
        
        int queued=unqueue(new_epoche_tokens);
        new_epoche_tokens-=queued;
        
        wake_up_interruptible(&wait_q);
        update_epoch_stats();
        atomic_inc(&sysThrot_dev.critical.epoch);
        atomic_set(&sysThrot_dev.critical.current_epoch_tokens, new_epoche_tokens); 
        if(sysThrot_dev.working) mod_timer(&sysThrot_dev.timer, jiffies + msecs_to_jiffies(EPOCH_DURATION_MS));
        free_junk();
}


int sysThrot_init(void) {
    printk("%s: initializing module\n", MODNAME);
    int ret;
    LOG(LOG_CORE,"%s: initializing", MODNAME);
    LOG(LOG_CORE,"%ld concurrent calls allowed", max_calls_monitor);
    init_sysThrot_store(&sysThrot_dev.store);
    ret = device_driver_init();
    if (ret < 0) {
        LOG(LOG_CORE,"%s: device driver init failed\n",MODNAME);
        return ret;
    }
    ret = sysThrot_statmonitor_init();
    if (ret < 0) {
        device_driver_cleanup();
        LOG(LOG_CORE,"%s: statmonitor init failed\n",MODNAME);
        return ret;
    }
    sysThrot_dev.working=0;
    sysThrot_dev.max_syscalls_for_epoch=max_calls_monitor;
    atomic_set(&sysThrot_dev.critical.threads_in_stub, 0);
    atomic_set(&sysThrot_dev.critical.epoch, 0);
    atomic_set(&sysThrot_dev.critical.current_epoch_tokens, 0);
    ret= sysThrot_turn_on();
    if (ret < 0) {
        sysThrot_statmonitor_exit();
        device_driver_cleanup();
        LOG(LOG_CORE,"%s: failed to turn on throttling\n",MODNAME);
        return ret;
    }
    LOG(LOG_CORE,"%s: module loaded\n", MODNAME);
    LOG(LOG_CORE,"module correctly mounted");
    return 0;
}

int sysThrot_turn_on(void){
    int ret;
    if(sysThrot_dev.working==1)
        return -EALREADY; //non so cos altro usare
    init_queue();
    timer_setup(&sysThrot_dev.timer, timer_callback, 0);
    ret=mod_timer(&sysThrot_dev.timer, jiffies + msecs_to_jiffies(EPOCH_DURATION_MS));
    if (ret) {
        LOG(LOG_CORE,"%s: Error in mod_timer\n", MODNAME);
        return ret;
    }
    sysThrot_dev.working=1;
    LOG(LOG_CORE,"Throttling turned ON");
    return 0;
}


int sysThrot_turn_off(void){
    if(sysThrot_dev.working==0)
        return -EALREADY; //non so cos altro usare#
    sysThrot_dev.working=0; 
    timer_shutdown_sync(&sysThrot_dev.timer);   
    wake_up_queue();
    while(atomic_read(&sysThrot_dev.critical.threads_in_module)>0){
            LOG(LOG_CORE,"Waiting for %d threads to exit the module before turning off throttling", atomic_read(&sysThrot_dev.critical.threads_in_module));
            wake_up_interruptible(&wait_q);
            msleep(100);

        }
    destroy_queue();
    LOG(LOG_CORE,"Throttling turned OFF");
    return 0;
}

void sysThrot_cleanup(void) {
    sysThrot_turn_off();
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
    int left;
    do{
        left=atomic_read(&sysThrot_dev.critical.threads_in_stub);
        if(left>0){
            LOG(LOG_CORE,"Waiting for %d threads to exit the stub before cleanup", left);
            msleep(100);
        }
    }while(left>0);
    synchronize_rcu();
    destroy_sysThrot_store(sysThrot_dev.store);
    device_driver_cleanup();
    sysThrot_statmonitor_exit();

    LOG(LOG_CORE,"shutting down");
    LOG(LOG_CORE,"%s: module unloaded\n", MODNAME);
}




module_init(sysThrot_init);
module_exit(sysThrot_cleanup);


