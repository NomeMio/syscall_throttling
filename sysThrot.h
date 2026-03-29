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


long int sysThrot_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

int sysThrot_register_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_deregister_user(TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int sysThrot_register_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_deregister_program(TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int sysThrot_register_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);
int sysThrot_deregister_syscall(TYPE_OF_DATA_PASSED_TO_IOCTL_SYSCALL syscall_id);





// list of syscalls symbols
