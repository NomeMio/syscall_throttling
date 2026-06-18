
#ifndef SYS_THROT_STORE_H
#define SYS_THROT_STORE_H

#include "sysThrot.h"
#include "sysThrot_ioctl.h"

struct users_list_array{
    int *users;
    int size;
};

struct programs_list_array{
    char **programs;
    int size;
};
struct syscall_list_array{
    int *syscalls;
    int size;
};


struct users_list_array *get_user_space_users_copy(void);


struct _sysThrot_Store;

int init_sysThrot_store(struct _sysThrot_Store **store);
int destroy_sysThrot_store(struct _sysThrot_Store *store);
int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
struct programs_list_array * get_user_space_programs_array_from_store(struct _sysThrot_Store *store);
struct users_list_array *get_user_space_users_array_from_store(struct _sysThrot_Store *store);

#endif

