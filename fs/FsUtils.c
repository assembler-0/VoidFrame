#include "FsUtils.h"
#include "Console.h"
#include "Fs.h"

int FsCat(const char* path) {
    int fd = FsOpen(path, FS_READ);
    if (fd < 0) {
        PrintKernel("cat: cannot open file\n");
        return -1;
    }
    
    char buffer[256];
    int bytes_read;
    
    while ((bytes_read = FsRead(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        PrintKernel(buffer);
    }
    
    FsClose(fd);
    return 0;
}

int FsLs(const char* path) {
    FsNode* dir = FsFind(path);
    if (!dir) {
        PrintKernel("ls: directory not found\n");
        return -1;
    }
    
    if (dir->type != FS_DIRECTORY) {
        PrintKernel("ls: not a directory\n");
        return -1;
    }
    
    FsNode* current = dir->children;
    if (!current) {
        PrintKernel("(empty directory)\n");
        return 0;
    }
    
    while (current) {
        if (current->type == FS_DIRECTORY) {
            PrintKernel("[DIR]  ");
        } else {
            PrintKernel("[FILE] ");
        }
        PrintKernel(current->name);
        PrintKernel(" (");
        PrintKernelInt(current->size);
        PrintKernel(" bytes)\n");
        current = current->next_sibling;
    }
    
    return 0;
}

int FsTouch(const char* path) {
    int fd = FsOpen(path, FS_WRITE);
    if (fd < 0) {
        PrintKernel("touch: cannot create file\n");
        return -1;
    }
    FsClose(fd);
    return 0;
}

int FsEcho(const char* text, const char* path) {
    int fd = FsOpen(path, FS_WRITE);
    if (fd < 0) {
        PrintKernel("echo: cannot open file\n");
        return -1;
    }
    
    int len = 0;
    while (text[len]) len++; // strlen
    
    int written = FsWrite(fd, text, len);
    FsClose(fd);
    
    return written == len ? 0 : -1;
}

void FsTest(void) {
    PrintKernel("[FS] Running filesystem tests...\n");
    
    // Create test directory
    if (FsMkdir("/test") == 0) {
        PrintKernel("[FS] Created /test directory\n");
    }
    
    // Create test file
    if (FsEcho("Hello VoidFrame!\n", "/test/hello.txt") == 0) {
        PrintKernel("[FS] Created /test/hello.txt\n");
    }
    
    // List root directory
    PrintKernel("[FS] Root directory contents:\n");
    FsLs("/");
    
    // List test directory
    PrintKernel("[FS] Test directory contents:\n");
    FsLs("/test");
    
    // Read test file
    PrintKernel("[FS] Contents of /test/hello.txt:\n");
    FsCat("/test/hello.txt");
    
    PrintKernel("[FS] Filesystem tests completed\n");
}