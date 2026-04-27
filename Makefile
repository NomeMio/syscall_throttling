obj-m += sysThrot_module.o
sysThrot_module-objs += sysThrot_store.o  sysThrot.o  sysThrot_ioctl.o  sysThrot_log.o


#syscall_address = $(shell cat /sys/module/the_usctm/pa0rameters/sys_call_table_address)

#sys_call_table_address=$(syscall_address)
all: generate_syscalls_header
	@make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules > output.log 2>&1

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod sysThrot_module.ko  max_calls_monitor=20

remove:
	rmmod sysThrot_module 


load_sys_calls:
	rm -f sys_calls_symbols.txt
	touch sys_calls_symbols.txt
	cat /proc/kallsyms | grep -oE ' __x64_sys_[^ ]*' >> sys_calls_symbols.txt

generate_syscalls_header: load_sys_calls
	rm -f syscalls.h
	@echo "#ifndef SYSCALLS_H" > syscalls.h
	@echo "#define SYSCALLS_H" >> syscalls.h
	@echo "" >> syscalls.h
	@echo "#define SUPPORTED_SYSCALLS $$(wc -l < sys_calls_symbols.txt)" >> syscalls.h
	@echo "" >> syscalls.h
	@echo "static const char *syscall_symbols[] = {" >> syscalls.h
	@awk '{gsub(/^[ \t]+|[ \t]+$$/, ""); print "\t\"" $$0 "\","}' sys_calls_symbols.txt | sed '$$s/,$$//' >> syscalls.h
	@echo "};" >> syscalls.h
	@echo "" >> syscalls.h
	@echo "#endif" >> syscalls.h
	rm -f sys_calls_symbols.txt