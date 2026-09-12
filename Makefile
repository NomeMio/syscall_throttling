obj-m += sysThrot_module.o
sysThrot_module-objs += sysThrot_store.o  sysThrot.o  sysThrot_ioctl.o  sysThrot_log.o sysThrot_queue.o sysThrot_statmonitor.o
ccflags-y += -I$(src)/lib

UNISTD_64 = $(firstword $(wildcard \
	/lib/modules/$(shell uname -r)/build/arch/x86/include/generated/uapi/asm/unistd_64.h \
	/usr/include/x86_64-linux-gnu/asm/unistd_64.h \
	/usr/include/asm/unistd_64.h))


all: generate_syscalls_header
	@make -C /lib/modules/$(shell uname -r)/build M=$(PWD) V=1 modules > output.log 2>&1

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod sysThrot_module.ko  max_calls_monitor=5

remove:
	rmmod sysThrot_module 


load_sys_calls:
	rm -f sys_calls_symbols.txt
	grep -oE '__x64_sys_[^ ]*' /proc/kallsyms | sort -u > /tmp/st_kallsyms.txt
	grep -Eh '^#define __NR_[a-z_0-9]+ [0-9]+' $(UNISTD_64) | \
		awk '{nr=$$3+0; name=$$2; sub(/^__NR_/, "", name); print nr, "__x64_sys_"name}' | \
		sort -k1 -n -u | \
		awk 'NR==FNR{sym[$$0]=1; next} sym[$$2]' /tmp/st_kallsyms.txt - > sys_calls_symbols.txt
	rm -f /tmp/st_kallsyms.txt

generate_syscalls_header: load_sys_calls
	rm -f lib/syscalls.h user/lib/syscalls.h
	@echo "#ifndef SYSCALLS_H" > lib/syscalls.h
	@echo "#define SYSCALLS_H" >> lib/syscalls.h
	@echo "" >> lib/syscalls.h
	@echo "#define SUPPORTED_SYSCALLS $$(awk 'END{print $$1+1}' sys_calls_symbols.txt)" >> lib/syscalls.h
	@echo "" >> lib/syscalls.h
	@echo "static const char *syscall_symbols[SUPPORTED_SYSCALLS] = {" >> lib/syscalls.h
	@awk '{print "\t[" $$1 "] = \"" $$2 "\","}' sys_calls_symbols.txt >> lib/syscalls.h
	@echo "};" >> lib/syscalls.h
	@echo "" >> lib/syscalls.h
	@echo "#endif" >> lib/syscalls.h
	cp lib/syscalls.h user/lib/syscalls.h
	rm -f sys_calls_symbols.txt
