#ifndef SYS_THROT_H
#define SYS_THROT_H

#include <asm/apic.h>
#include <linux/syscalls.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include "syscalls.h"


#define MODNAME "SYSTHROT"
#define DEVICE_NAME "sysThrot_dev"
#define EPOCH_DURATION_MS 100
#define sysThrot_IOC_MAGIC '-'


struct sysThrot_critical
{
    
    atomic_t threads_in_stub;
    atomic_t threads_in_module;
    atomic_t current_epoch_tokens;
    atomic_t epoch;

};

struct sysThrot_driver
{   
    unsigned working;
    struct sysThrot_critical critical;
    struct _sysThrot_Store *store;
    int max_syscalls_for_epoch; 
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




//LOG STUFF




#define LOG_CORE 1
#define LOG_IOCTL 1<<1
#define LOG_ADMIN_COMMANDS 1<<2
#define LOG_STATS 1<<4
#define LOG_STUB 1<<5
#define LOG_CHECK_IF_LIMITED 1<<6
#define LOG_STORE 1<<7
#define LOG_LEVEL (LOG_CORE| LOG_STORE |LOG_CHECK_IF_LIMITED |LOG_IOCTL | LOG_ADMIN_COMMANDS | LOG_STATS)
void sysThrot_log(const char *fmt, ...);

#define LOG(level, ...) do { if (LOG_LEVEL & (level)) sysThrot_log(__VA_ARGS__); } while (0)




// MEM MANIPULATION FUNCTIONS





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


// check if calling this is safe
static inline void being_sys_call_hacking(unsigned long *cr0_ptr, unsigned long *cr4_ptr){
    preempt_disable(); 
    *cr0_ptr = read_cr0();
    *cr4_ptr = native_read_cr4();
    conditional_cet_disable(*cr4_ptr);
    unprotect_memory();
}

static inline void end_sys_call_hacking(unsigned long cr0, unsigned long cr4){
    protect_memory();
    conditional_cet_enable(cr4);
    preempt_enable(); 
}

#endif