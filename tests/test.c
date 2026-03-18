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
//#include "./include/vtpmo.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nessuno");
MODULE_DESCRIPTION("Test Module");


#define MODNAME "test"

unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);




int test_init(void) {
	
		
    printk("%s: initializing\n",MODNAME);
	

    if (sys_call_table_address == 0x0) {
        printk("%s: sys_call_table_address parameter not set\n",MODNAME);
        return -EINVAL;
    } else {
        printk("%s: sys_call_table_address parameter set to 0x%lx\n",MODNAME,sys_call_table_address);
    }








    printk("%s: module correctly mounted\n",MODNAME);

    return 0;

}


void test_cleanup(void) {
    printk("%s: shutting down\n",MODNAME);
        
}




module_init(test_init);
module_exit(test_cleanup);
