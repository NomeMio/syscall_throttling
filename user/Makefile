compile_client: generate_syscalls_header
	gcc -o client client.c -I./lib




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
	mv syscalls.h ./lib/syscalls.h