#ifndef STDARG_H
#define STDARG_H

// Custom stdarg implementation for freestanding environments (GCC/Clang)

typedef char* va_list;

#define va_start(v,l)   __builtin_va_start(v,l)
#define va_arg(v,t)     __builtin_va_arg(v,t)
#define va_end(v)       __builtin_va_end(v)

#endif // STDARG_H
