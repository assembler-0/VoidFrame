#ifndef VOIDFRAME_KERNEL_H
#define VOIDFRAME_KERNEL_H
extern int CurrentLine;
extern int CurrentColumn;
void ClearScreen();
void PrintKernel(const char *str);
void PrintKernelInt(int num);
void PrintKernelHex(int hex);
void PrintKernelAt(const char *str, int line, int col);
#endif
