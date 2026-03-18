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
MODULE_DESCRIPTION("Syscall Throttling Module");


#define MODNAME "SYSTHROT"

unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);




int sysThrot_init(void) {
	
	int i,j;
		
    printk("%s: initializing\n",MODNAME);
	

    printk("%s: module correctly mounted\n",MODNAME);

    return 0;

}


void sysThrot_cleanup(void) {
    printk("%s: shutting down\n",MODNAME);
        
}




module_init(sysThrot_init);
module_exit(sysThrot_cleanup);
