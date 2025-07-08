#ifndef KERNEL_H
#define KERNEL_H
extern int CurrentLine;
extern int CurrentColumn;
void ClearScreen();
void PrintKernel(const char *str);
void PrintKernelInt(int num);
void PrintKernelHex(uint64_t hex);
void PrintKernelAt(const char *str, int line, int col);
void FastPrint(const char *str, int line, int col);
void FastPrintHex(uint64_t num, int line, int col);
void FastPrintChar(char c, int line, int col);
#endif
