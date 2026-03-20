obj-m += sysThrot_module.o
sysThrot_module-objs += sysThrot.o 


#syscall_address = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)

#sys_call_table_address=$(syscall_address)
all:
	@make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules > output.log 2>&1

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod sysThrot_module.ko  max_calls_monitor=10 

remove:
	rmmod sysThrot_module 

