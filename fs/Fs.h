#ifndef FS_H
#define FS_H

#include "stdint.h"
#include "stddef.h"

#define MAX_FILENAME 64
#define MAX_PATH 256
#define MAX_OPEN_FILES 32
#define MAX_FS_NODES 128

typedef enum {
    FS_FILE,
    FS_DIRECTORY
} FsNodeType;

typedef enum {
    FS_READ = 1,
    FS_WRITE = 2,
    FS_APPEND = 4
} FsOpenFlags;

typedef struct FsNode {
    char name[MAX_FILENAME];
    FsNodeType type;
    uint64_t size;
    uint64_t created_time;
    uint64_t modified_time;
    void* data;
    struct FsNode* parent;
    struct FsNode* children;
    struct FsNode* next_sibling;
    uint32_t node_id;
} FsNode;

typedef struct {
    FsNode* node;
    uint64_t position;
    FsOpenFlags flags;
    uint32_t fd;
    uint32_t owner_pid;
} FileHandle;

// Core filesystem functions
int FsInit(void);
FsNode* FsCreateNode(const char* name, FsNodeType type, FsNode* parent);
FsNode* FsFind(const char* path);
int FsDelete(const char* path);

// File operations
int FsOpen(const char* path, FsOpenFlags flags);
int FsClose(int fd);
int FsRead(int fd, void* buffer, size_t size);
int FsWrite(int fd, const void* buffer, size_t size);
int FsSeek(int fd, uint64_t position);

// Directory operations
int FsMkdir(const char* path);
int FsRmdir(const char* path);
FsNode* FsReaddir(const char* path);
int FsListDir(const char* path);
int FsCreateFile(const char* path);
int FsCreateDir(const char* path);
int FsWriteFile(const char* path, const void* buffer, size_t size);

#endif