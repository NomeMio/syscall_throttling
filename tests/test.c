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
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <linux/syscalls.h>
#include "lib/include/scth.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Test Module");
#define SYS_TO_REMOVE 153

#define MODNAME "test"
unsigned long sys_call_fork_address = 0x0;
unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);




#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _print, unsigned long, none){
#else
asmlinkage long sys_print(void){
#endif
	printk(KERN_DEBUG "%s: message priority %s\n",MODNAME, KERN_DEBUG);
	return 0;
	
}


long sys_print = (unsigned long) __x64_sys_print;      //57

int test_init(void) {
	
		
    printk("%s: initializing\n",MODNAME);
	

    if (sys_call_table_address == 0x0) {
        printk("%s: sys_call_table_address parameter not set\n",MODNAME);
        return -EINVAL;
    } else {
        printk("%s: sys_call_table_address parameter set to 0x%lx\n",MODNAME,sys_call_table_address);
    }





    unprotect_memory();
    sys_call_fork_address = ((unsigned long *)sys_call_table_address)[SYS_TO_REMOVE];
    ((unsigned long *)sys_call_table_address)[SYS_TO_REMOVE] = (unsigned long)sys_print;
    protect_memory();
    printk("%s: module correctly mounted\n",MODNAME);

    return 0;

}


void test_cleanup(void) {

    unprotect_memory();
    ((unsigned long *)sys_call_table_address)[SYS_TO_REMOVE] = sys_call_fork_address;
    protect_memory();


    printk("%s: shutting down\n",MODNAME);
        
}




module_init(test_init);
module_exit(test_cleanup);
