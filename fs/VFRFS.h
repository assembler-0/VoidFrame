#ifndef FS_H
#define FS_H

#include "stdint.h"
#include "stddef.h"

#define MAX_FILENAME 64
#define MAX_PATH 256
#define MAX_OPEN_FILES 32
#define MAX_FS_NODES 128

// Seek whence values
#define SEEK_SET 0 // Seek from the beginning of the file
#define SEEK_CUR 1 // Seek from the current position
#define SEEK_END 2 // Seek from the end of the file

// Filesystem node types
typedef enum {
    FS_FILE,
    FS_DIRECTORY
} FsNodeType;

// File open flags
typedef enum {
    FS_READ = 1,
    FS_WRITE = 2,
    FS_APPEND = 4
} FsOpenFlags;

// Represents a file or directory in the filesystem
typedef struct FsNode {
    char name[MAX_FILENAME];
    FsNodeType type;
    uint64_t size;
    uint64_t created_time;   // Timestamp of creation
    uint64_t modified_time;  // Timestamp of last modification
    void* data;              // File content for FS_FILE types
    struct FsNode* parent;

    // For directories, a linked list of children
    struct FsNode* children;

    // --- NEW --- For O(1) child insertion
    struct FsNode* last_child;

    // Doubly-linked list for siblings
    struct FsNode* next_sibling;
    struct FsNode* prev_sibling;

    uint32_t node_id; // Unique identifier for the node
} FsNode;

// Represents an open file handle
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
int FsDeleteRecursive(const char* path); // New recursive delete function

// File operations
int FsOpen(const char* path, FsOpenFlags flags);
int FsClose(int fd);
int FsRead(int fd, void* buffer, size_t size);
int FsWrite(int fd, const void* buffer, size_t size);
int64_t FsSeek(int fd, int64_t offset, int whence);

// Directory operations
int FsMkdir(const char* path);
int FsRmdir(const char* path);
FsNode* FsReaddir(const char* path);
int FsListDir(const char* path);

// Helper/utility functions
int FsCreateFile(const char* path);
int FsWriteFile(const char* path, const void* buffer, size_t size);

#endif