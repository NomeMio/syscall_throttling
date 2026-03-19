obj-m += test_module.o
test_module-objs += test.o lib/scth.o

syscall_address = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)



all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod test_module.ko sys_call_table_address=$(syscall_address)

remove:
	rmmod test_module 

