#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "Fs.h"

// Utility functions
int FsCat(const char* path);
int FsLs(const char* path);
int FsTouch(const char* path);
int FsEcho(const char* text, const char* path);
void FsTest(void);

#endif