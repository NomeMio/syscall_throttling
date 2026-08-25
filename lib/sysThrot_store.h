
#ifndef SYS_THROT_STORE_H
#define SYS_THROT_STORE_H

#include "sysThrot.h"
#include "sysThrot_ioctl.h"

#define MAX_STORE_PROGRAMS 64





struct _sysThrot_Store;

int init_sysThrot_store(struct _sysThrot_Store **store);
int destroy_sysThrot_store(struct _sysThrot_Store *store);
int add_user_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int remove_user_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int find_user_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_USER user_id);
int add_program_to_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int remove_program_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int find_program_in_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM program_id);
int get_all_users_from_store(struct _sysThrot_Store *store, int *user_array, int *num_users);
int get_all_programs_from_store(struct _sysThrot_Store *store, TYPE_OF_DATA_PASSED_TO_IOCTL_PROGRAM *program_array, int *num_programs);


#endif

