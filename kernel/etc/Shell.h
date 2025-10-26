#ifndef SHELL_H
#define SHELL_H

void ShellInit(void);
void ShellProcess(void);
void ExecuteCommand(const char* cmd);
char* GetArg(const char* cmd, int arg_num);

#endif