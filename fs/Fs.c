#include "Fs.h"
#include "Console.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Process.h"

static FsNode* root_node = NULL;
static FileHandle file_handles[MAX_OPEN_FILES];
static FsNode fs_nodes[MAX_FS_NODES];
static uint32_t next_node_id = 1;
static uint32_t next_fd = 1;

static FsNode* AllocNode(void) {
    for (int i = 0; i < MAX_FS_NODES; i++) {
        if (fs_nodes[i].node_id == 0) {
            fs_nodes[i].node_id = next_node_id++;
            return &fs_nodes[i];
        }
    }
    return NULL;
}

static void FreeNode(FsNode* node) {
    if (node && node >= fs_nodes && node < fs_nodes + MAX_FS_NODES) {
        FastMemset(node, 0, sizeof(FsNode));
    }
}

static FileHandle* AllocHandle(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_handles[i].fd == 0) {
            file_handles[i].fd = next_fd++;
            return &file_handles[i];
        }
    }
    return NULL;
}

static FileHandle* GetHandle(int fd) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_handles[i].fd == fd) {
            return &file_handles[i];
        }
    }
    return NULL;
}

int FsInit(void) {
    FastMemset(file_handles, 0, sizeof(file_handles));
    FastMemset(fs_nodes, 0, sizeof(fs_nodes));
    
    root_node = AllocNode();
    if (!root_node) return -1;
    
    FastMemcpy(root_node->name, "/", 2);
    root_node->type = FS_DIRECTORY;
    root_node->parent = NULL;
    
    // Create standard directories
    FsCreateNode("System", FS_DIRECTORY, root_node);
    FsCreateNode("Core", FS_DIRECTORY, root_node);
    FsCreateNode("Tmp", FS_DIRECTORY, root_node);
    FsCreateNode("Home", FS_DIRECTORY, root_node);
    
    PrintKernelSuccess("[SYSTEM] Created standard directories\n");
    return 0;
}

FsNode* FsCreateNode(const char* name, FsNodeType type, FsNode* parent) {
    if (!name || !parent) return NULL;
    
    FsNode* node = AllocNode();
    if (!node) return NULL;
    
    FastMemcpy(node->name, name, MAX_FILENAME);
    node->type = type;
    node->parent = parent;
    node->size = 0;
    node->data = NULL;
    
    // Add to parent's children
    if (!parent->children) {
        parent->children = node;
    } else {
        FsNode* sibling = parent->children;
        while (sibling->next_sibling) {
            sibling = sibling->next_sibling;
        }
        sibling->next_sibling = node;
    }
    
    return node;
}

FsNode* FsFind(const char* path) {
    if (!path || !root_node) return NULL;
    
    if (path[0] == '/' && path[1] == '\0') {
        return root_node;
    }
    
    FsNode* current = root_node;
    char name_buf[MAX_FILENAME];
    int name_idx = 0;
    
    for (int i = 1; path[i]; i++) {
        if (path[i] == '/' || path[i + 1] == '\0') {
            if (path[i] != '/') {
                name_buf[name_idx++] = path[i];
            }
            name_buf[name_idx] = '\0';
            
            if (name_idx == 0) continue;
            
            FsNode* child = current->children;
            while (child) {
                if (FastMemcmp(child->name, name_buf, name_idx + 1) == 0) {
                    current = child;
                    break;
                }
                child = child->next_sibling;
            }
            
            if (!child) return NULL;
            name_idx = 0;
        } else {
            if (name_idx < MAX_FILENAME - 1) {
                name_buf[name_idx++] = path[i];
            }
        }
    }
    
    return current;
}

int FsOpen(const char* path, FsOpenFlags flags) {
    FsNode* node = FsFind(path);
    if (!node && !(flags & FS_WRITE)) return -1;
    
    if (!node && (flags & FS_WRITE)) {
        // Create new file
        char parent_path[MAX_PATH];
        char filename[MAX_FILENAME];
        
        // Extract parent path and filename
        int last_slash = -1;
        for (int i = 0; path[i]; i++) {
            if (path[i] == '/') last_slash = i;
        }
        
        if (last_slash == -1) return -1;
        
        FastMemcpy(parent_path, path, last_slash + 1);
        parent_path[last_slash + 1] = '\0';
        FastMemcpy(filename, path + last_slash + 1, MAX_FILENAME);
        
        FsNode* parent = FsFind(parent_path);
        if (!parent || parent->type != FS_DIRECTORY) return -1;
        
        node = FsCreateNode(filename, FS_FILE, parent);
        if (!node) return -1;
    }
    
    if (node->type != FS_FILE) return -1;
    
    FileHandle* handle = AllocHandle();
    if (!handle) return -1;
    
    handle->node = node;
    handle->position = (flags & FS_APPEND) ? node->size : 0;
    handle->flags = flags;
    handle->owner_pid = GetCurrentProcess()->pid;
    
    return handle->fd;
}

int FsClose(int fd) {
    FileHandle* handle = GetHandle(fd);
    if (!handle) return -1;
    
    FastMemset(handle, 0, sizeof(FileHandle));
    return 0;
}

int FsRead(int fd, void* buffer, size_t size) {
    FileHandle* handle = GetHandle(fd);
    if (!handle || !(handle->flags & FS_READ) || !buffer) return -1;
    
    FsNode* node = handle->node;
    if (!node || !node->data) return 0;
    
    uint64_t available = node->size - handle->position;
    if (size > available) size = available;
    
    FastMemcpy(buffer, (char*)node->data + handle->position, size);
    handle->position += size;
    
    return size;
}

int FsWrite(int fd, const void* buffer, size_t size) {
    FileHandle* handle = GetHandle(fd);
    if (!handle || !(handle->flags & FS_WRITE) || !buffer) return -1;
    
    FsNode* node = handle->node;
    if (!node) return -1;
    
    uint64_t new_size = handle->position + size;
    if (new_size > node->size) {
        void* new_data = KernelReallocate(node->data, new_size);
        if (!new_data) return -1;
        node->data = new_data;
        node->size = new_size;
    }
    
    FastMemcpy((char*)node->data + handle->position, buffer, size);
    handle->position += size;
    
    return size;
}

int FsMkdir(const char* path) {
    if (!path) return -1;
    
    char parent_path[MAX_PATH];
    char dirname[MAX_FILENAME];
    
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash == -1) return -1;
    
    // Copy parent path
    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        FastMemcpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
    }
    
    // Copy directory name
    int name_len = 0;
    const char* name_start = path + last_slash + 1;
    while (name_start[name_len] && name_len < MAX_FILENAME - 1) {
        dirname[name_len] = name_start[name_len];
        name_len++;
    }
    dirname[name_len] = '\0';
    
    if (name_len == 0) return -1;
    
    FsNode* parent = FsFind(parent_path);
    if (!parent || parent->type != FS_DIRECTORY) return -1;
    
    FsNode* new_dir = FsCreateNode(dirname, FS_DIRECTORY, parent);
    return new_dir ? 0 : -1;
}

int FsDelete(const char* path) {
    if (!path) return -1;
    
    FsNode* node = FsFind(path);
    if (!node) return -1;
    
    // Can't delete root
    if (!node->parent) return -1;
    
    // Can't delete non-empty directories
    if (node->type == FS_DIRECTORY && node->children) return -1;
    
    // Remove from parent's children list
    FsNode* parent = node->parent;
    if (parent->children == node) {
        parent->children = node->next_sibling;
    } else {
        FsNode* sibling = parent->children;
        while (sibling && sibling->next_sibling != node) {
            sibling = sibling->next_sibling;
        }
        if (sibling) {
            sibling->next_sibling = node->next_sibling;
        }
    }
    
    // Free file data if it exists
    if (node->data) {
        KernelFree(node->data);
    }
    
    // Free the node
    FreeNode(node);
    return 0;
}

FsNode* FsReaddir(const char* path) {
    FsNode* dir = FsFind(path);
    if (!dir || dir->type != FS_DIRECTORY) return NULL;
    return dir->children;
}

int FsListDir(const char* path) {
    FsNode* dir = FsFind(path);
    if (!dir || dir->type != FS_DIRECTORY) return -1;
    
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

int FsCreateFile(const char* path) {
    int fd = FsOpen(path, FS_WRITE);
    if (fd < 0) return -1;
    FsClose(fd);
    return 0;
}

int FsCreateDir(const char* path) {
    return FsMkdir(path);
}

int FsWriteFile(const char* path, const void* buffer, uint32_t size) {
    int fd = FsOpen(path, FS_WRITE);
    if (fd < 0) return -1;
    int result = FsWrite(fd, buffer, size);
    FsClose(fd);
    return result;
}