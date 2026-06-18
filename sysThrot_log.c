#include "sysThrot.h"





void inline sysThrot_log(const char *fmt, ...) {
    
    //Create string from fmt and variable arguments
    char kfmt[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(kfmt, sizeof(kfmt), fmt, args);
    va_end(args);
    printk(kfmt);
    }

   