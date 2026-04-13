#include <asm/apic.h>
#include <linux/syscalls.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include "./syscalls.h"


#define  AUDIT if(1)
#define MODNAME "SYSTHROT"
#define DEVICE_NAME "sysThrot_dev"
#define EPOCH_DURATION_MS 1000
#define sysThrot_IOC_MAGIC '-'

#define SYS_AUDIT_LOG(fmt, ...) sysThrot_log("%s: " fmt, MODNAME, ##__VA_ARGS__)

#define TYPE_OF_DATA_PASSED_TO_IOCTL_USER int*
#define TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM char *
#define TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL int *

#define sysThrot_IOC_REGISTER_USER _IOW(sysThrot_IOC_MAGIC, 1, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_DEREGISTER_USER _IOW(sysThrot_IOC_MAGIC, 2, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_REGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 3, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_DEREGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 4, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_REGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 5, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)
#define sysThrot_IOC_DEREGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 6, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)
#define sysThrot_IOC_TURN_ON _IO(sysThrot_IOC_MAGIC, 7)
#define sysThrot_IOC_TURN_OFF _IO(sysThrot_IOC_MAGIC, 8)





struct _queue_elem{
    struct task_struct *task;
    int was_interrupted;
    int *to_wake;
    struct _queue_elem *next;
};





/*
* @threads_in_stub:
*       Tenuto per controllare quanti thread si trovano al iterno dello stub che il modulo monta nelle syscall,
*       cosi da evitare errori di accesso in memoria da parte del kernel in fase di smontaggio
* @threads_in_module:
*       BLA
* 
*
*/



struct sysThrot_critical
{
    
    atomic_t threads_in_stub;
    atomic_t threads_in_module;
    atomic_t current_epoch_tokens;
    spinlock_t queue_lock;
    spinlock_t interrupt_lock;
    struct _queue_elem sentinel_head;//.next has first element
    struct _queue_elem sentinel_tail;//.next has last element

    atomic_t epoch;

};






struct sysThrot_driver
{   
    unsigned working;
    struct sysThrot_critical critical;
    struct _sysThrot_Store *store;
    int max_syscalls_for_epoch; //TODO da vedere se lasciarlo int visto che atomic_t al massimo e' int signed
    char syscall_presence_bitmap[__NR_syscalls/8+1];
    int bitmap_size;
    unsigned long syscall_addresses[SUPPORTED_SYSCALLS];
    struct file_operations fops;
    struct cdev cdev;
    struct class *device_class;
    struct timer_list timer;
};




extern struct sysThrot_driver sysThrot_dev;
extern int major_number;
extern int minor_number;
extern const struct file_operations fops;

int device_driver_init(void);
int device_driver_cleanup(void);


long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);
int sysThrot_turn_off(void);
int sysThrot_turn_on(void);

struct _sysThrot_Store;

int init_sysThrot_store(struct _sysThrot_Store **store);
int destroy_sysThrot_store(struct _sysThrot_Store *store);
int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_turn_on(void);
int sysThrot_turn_off(void);

int sysThrot_log_init(void);
void sysThrot_log_cleanup(void);
void sysThrot_log(const char *fmt, ...);



/* Memory Protection Functions */
static inline void write_cr0_forced(unsigned long val){
    unsigned long __force_order;
    asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(__force_order));
}

static inline void protect_memory(void){
    unsigned long cr0 = read_cr0();
    write_cr0_forced(cr0);
}

static inline void unprotect_memory(void){
    unsigned long cr0 = read_cr0();
    write_cr0_forced(cr0 & ~X86_CR0_WP);
}

static inline void write_cr4_forced(unsigned long val){
    unsigned long __force_order;
    asm volatile("mov %0, %%cr4" : "+r"(val), "+m"(__force_order));
}

static inline void conditional_cet_disable(unsigned long cr4){
#ifdef X86_CR4_CET
    if (cr4 & X86_CR4_CET)
        write_cr4_forced(cr4 & ~X86_CR4_CET);
#endif
}

static inline void conditional_cet_enable(unsigned long cr4){
#ifdef X86_CR4_CET
    if (cr4 & X86_CR4_CET)
        write_cr4_forced(cr4);
#endif
}

static inline void being_sys_call_hacking(unsigned long *cr0_ptr, unsigned long *cr4_ptr){
    //preempt_disable(); let calling function to handle preemption
    *cr0_ptr = read_cr0();
    *cr4_ptr = native_read_cr4();
    conditional_cet_disable(*cr4_ptr);
    unprotect_memory();
}

static inline void end_sys_call_hacking(unsigned long cr0, unsigned long cr4){
    protect_memory();
    conditional_cet_enable(cr4);
    //preempt_enable(); let calling function to handle preemption
}