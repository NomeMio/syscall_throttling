compile_client: generate_syscalls_header
	gcc -o client client.c -I./lib
	gcc -o systhrot systhrot.c -I./lib

register: systhrot
	./systhrot register-user 1000
	./systhrot register-program client
	./systhrot register-syscall __x64_sys_getpid

deregister: systhrot
	./systhrot deregister-syscall __x64_sys_getpid

clean:
	rm -f client systhrot
	rm -f lib/syscalls.h

load_sys_calls:
	rm -f sys_calls_symbols.txt
	grep -oE '__x64_sys_[^ ]*' /proc/kallsyms | sort -u > /tmp/st_kallsyms.txt
	grep -Eh '^#define __NR_[a-z_0-9]+ [0-9]+' \
		/usr/include/x86_64-linux-gnu/asm/unistd_64.h \
		/usr/include/asm/unistd_64.h 2>/dev/null | \
		sort -u | \
		awk '{name=$$2; sub(/^__NR_/, "", name); print $$3+0, "__x64_sys_"name}' | \
		sort -k1 -n | \
		awk '{print $$2}' | \
		grep -Fxf /tmp/st_kallsyms.txt > sys_calls_symbols.txt
	rm -f /tmp/st_kallsyms.txt

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
	mv syscalls.h ./lib/syscalls.h