#include <asm/apic.h>
#include <linux/syscalls.h>

#define sysThrot_IOC_MAGIC '-'

#define TYPE_OF_DATA_PASSED_TO_IOCTL_USER int
#define TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM char *
#define TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL int

#define sysThrot_IOC_REGISTER_USER _IOW(sysThrot_IOC_MAGIC, 1, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_DEREGISTER_USER _IOW(sysThrot_IOC_MAGIC, 2, TYPE_OF_DATA_PASSED_TO_IOCTL_USER)
#define sysThrot_IOC_REGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 3, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_DEREGISTER_PROGRAM _IOW(sysThrot_IOC_MAGIC, 4, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM)
#define sysThrot_IOC_REGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 5, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)
#define sysThrot_IOC_DEREGISTER_SYSCALL _IOW(sysThrot_IOC_MAGIC, 6, TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL)

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

/* IOCTL Operations */
long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);

/* List Management Functions */
void init_lists(void);
void destroy_lists(void);
int user_list_add(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int user_list_remove(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int program_list_add(const char __user *user_program_name);
int program_list_remove(const char __user *user_program_name);
int program_list_find(const char *program_name);
void print_program_list(void);


// list of syscalls symbols
