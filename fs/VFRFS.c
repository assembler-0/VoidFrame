#include "Console.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Scheduler.h"
#include "StringOps.h"
#include "VFRFS.h"

// --- Static Globals ---
static FsNode* root_node = NULL;
static FileHandle file_handles[MAX_OPEN_FILES];
static FsNode fs_nodes[MAX_FS_NODES];

// --- IMPROVED: "Next free" hints to speed up allocation ---
static uint32_t next_node_idx_hint = 0;
static uint32_t next_handle_idx_hint = 0;

static uint32_t next_node_id = 1;
static uint32_t next_fd = 1;

// --- Forward Declarations for new helper functions ---
static void FsDeleteRecursiveI(FsNode* node);
static FsNode* FsFindParent(const char* path, char* child_name_out);

// --- Time placeholder ---
static uint64_t GetCurrentTime(void) {
    return 0;
}

// --- Node and Handle Management ---

static FsNode* AllocNode(void) {
    for (int i = 0; i < MAX_FS_NODES; i++) {
        uint32_t idx = (next_node_idx_hint + i) % MAX_FS_NODES;
        if (fs_nodes[idx].node_id == 0) {
            fs_nodes[idx].node_id = next_node_id++;
            next_node_idx_hint = idx + 1;
            return &fs_nodes[idx];
        }
    }
    return NULL; // No free nodes
}

static void FreeNode(FsNode* node) {
    if (node && node >= fs_nodes && node < fs_nodes + MAX_FS_NODES) {
        if (node->data) {
            KernelFree(node->data);
        }
        FastMemset(node, 0, sizeof(FsNode));
    }
}

static FileHandle* AllocHandle(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        uint32_t idx = (next_handle_idx_hint + i) % MAX_OPEN_FILES;
        if (file_handles[idx].fd == 0) {
            file_handles[idx].fd = next_fd++;
            if (next_fd == 0) next_fd = 1; // Avoid fd=0 and handle wrap-around
            next_handle_idx_hint = idx + 1;
            return &file_handles[idx];
        }
    }
    return NULL; // No free handles
}

static FileHandle* GetHandle(int fd) {
    if (fd <= 0) return NULL;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_handles[i].fd == fd) {
            return &file_handles[i];
        }
    }
    return NULL;
}

// --- Core Filesystem Logic ---

int FsInit(void) {
    FastMemset(file_handles, 0, sizeof(file_handles));
    FastMemset(fs_nodes, 0, sizeof(fs_nodes));

    root_node = AllocNode();
    if (!root_node) return -1;

    FastMemcpy(root_node->name, "/", 2);
    root_node->type = FS_DIRECTORY;
    root_node->created_time = GetCurrentTime();
    root_node->modified_time = root_node->created_time;

    PrintKernelSuccess("VFRFS: Filesystem initialized.\n");
    return 0;
}

FsNode* FsCreateNode(const char* name, FsNodeType type, FsNode* parent) {
    if (!name || !parent || parent->type != FS_DIRECTORY || name[0] == '\0') {
        return NULL;
    }

    // Check if a node with the same name already exists
    for (FsNode* child = parent->children; child; child = child->next_sibling) {
        if (FastStrCmp(child->name, name) == 0) {
            return NULL; // Already exists
        }
    }

    FsNode* node = AllocNode();
    if (!node) return NULL;

    FastStrCopy(node->name, name, MAX_FILENAME - 1);
    node->name[MAX_FILENAME - 1] = '\0';
    node->type = type;
    node->parent = parent;
    node->created_time = GetCurrentTime();
    node->modified_time = node->created_time;

    // --- IMPROVED: O(1) insertion into parent's children list ---
    if (!parent->last_child) { // First child
        parent->children = node;
        parent->last_child = node;
    } else {
        node->prev_sibling = parent->last_child;
        parent->last_child->next_sibling = node;
        parent->last_child = node;
    }

    parent->modified_time = GetCurrentTime();
    return node;
}

FsNode* FsFind(const char* path) {
    if (!path || !root_node) return NULL;

    // Handle the root case immediately
    if (path[0] == '/' && path[1] == '\0') {
        return root_node;
    }

    FsNode* current = root_node;
    const char* p = path;
    char name_buf[MAX_FILENAME];

    while (*p) {
        // Skip leading slashes
        while (*p == '/') p++;
        if (*p == '\0') break; // Handle trailing slashes

        // Extract the next path component
        int i = 0;
        while (*p && *p != '/' && i < MAX_FILENAME - 1) {
            name_buf[i++] = *p++;
        }
        name_buf[i] = '\0';

        if (name_buf[0] == '\0') continue;

        // Handle '.' and '..'
        if (FastStrCmp(name_buf, ".") == 0) {
            continue;
        }
        if (FastStrCmp(name_buf, "..") == 0) {
            if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        // Search for the component in the current directory's children
        FsNode* child = current->children;
        while (child) {
            if (FastStrCmp(child->name, name_buf) == 0) {
                current = child;
                break;
            }
            child = child->next_sibling;
        }

        if (!child) return NULL; // Component not found
    }

    return current;
}


// --- File Operations ---

int FsOpen(const char* path, FsOpenFlags flags) {
    FsNode* node = FsFind(path);

    // If the file doesn't exist, create it if write flag is set
    if (!node) {
        if (flags & FS_WRITE) {
            char filename[MAX_FILENAME];
            FsNode* parent = FsFindParent(path, filename);
            if (!parent) return -1; // Invalid parent path

            node = FsCreateNode(filename, FS_FILE, parent);
            if (!node) return -1; // Creation failed
        } else {
            return -1; // Not found and not creating
        }
    }

    if (node->type != FS_FILE) return -1; // Can't open directories with FsOpen

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
    if (!node || !node->data) return 0; // Reading from an empty file

    uint64_t readable_size = node->size - handle->position;
    size_t bytes_to_read = (size > readable_size) ? readable_size : size;

    if (bytes_to_read > 0) {
        FastMemcpy(buffer, (char*)node->data + handle->position, bytes_to_read);
        handle->position += bytes_to_read;
    }

    return bytes_to_read;
}

int FsWrite(int fd, const void* buffer, size_t size) {
    FileHandle* handle = GetHandle(fd);
    if (!handle || !(handle->flags & FS_WRITE) || !buffer) return -1;
    if (size == 0) return 0;

    FsNode* node = handle->node;
    if (!node) return -1;

    uint64_t new_size = handle->position + size;
    if (new_size > node->size) {
        void* new_data = KernelReallocate(node->data, new_size);
        if (!new_data) return -1; // Out of memory
        node->data = new_data;
        node->size = new_size;
    }

    FastMemcpy((char*)node->data + handle->position, buffer, size);
    handle->position += size;
    node->modified_time = GetCurrentTime();

    return size;
}

int64_t FsSeek(int fd, int64_t offset, int whence) {
    FileHandle* handle = GetHandle(fd);
    if (!handle || !handle->node) return -1;

    int64_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int64_t)handle->position + offset;
            break;
        case SEEK_END:
            new_pos = (int64_t)handle->node->size + offset;
            break;
        default:
            return -1; // Invalid whence
    }

    if (new_pos < 0) return -1; // Seeking before start of file

    // Unlike before, we now allow seeking past EOF. The file size will
    // grow upon the next write.
    handle->position = (uint64_t)new_pos;
    return new_pos;
}

// --- Directory and Deletion Operations ---

int FsMkdir(const char* path) {
    char dirname[MAX_FILENAME];
    FsNode* parent = FsFindParent(path, dirname);
    if (!parent) return -1;

    FsNode* new_dir = FsCreateNode(dirname, FS_DIRECTORY, parent);
    return new_dir ? 0 : -1;
}

static void UnlinkNode(FsNode* node) {
    if (!node || !node->parent) return;

    FsNode* parent = node->parent;
    if (node->prev_sibling) {
        node->prev_sibling->next_sibling = node->next_sibling;
    } else {
        parent->children = node->next_sibling;
    }

    if (node->next_sibling) {
        node->next_sibling->prev_sibling = node->prev_sibling;
    } else {
        // This was the last child
        parent->last_child = node->prev_sibling;
    }

    parent->modified_time = GetCurrentTime();
}

int FsDelete(const char* path) {
    FsNode* node = FsFind(path);
    if (!node || !node->parent) return -1; // Cannot delete root

    // Can't delete non-empty directories with this function
    if (node->type == FS_DIRECTORY && node->children) return -1;

    UnlinkNode(node);
    FreeNode(node);
    return 0;
}

int FsRmdir(const char* path) {
    FsNode* node = FsFind(path);
    if (!node || node->type != FS_DIRECTORY) return -1; // Not a directory
    return FsDelete(path); // FsDelete already checks if it's empty
}


// --- NEW --- Recursive Deletion (`rm -rf`)

/**
 * @brief Deletes a node and, if it's a directory, all its contents recursively.
 * @param node The node to start deleting from.
 */
static void FsDeleteRecursiveI(FsNode* node) {
    if (!node) return;

    // If it's a directory, recursively delete all its children first
    if (node->type == FS_DIRECTORY) {
        FsNode* child = node->children;
        while (child) {
            FsNode* next = child->next_sibling; // Get next before child is freed
            FsDeleteRecursiveI(child);
            child = next;
        }
    }

    // Now, free the node itself
    FreeNode(node);
}

/**
 * @brief Public interface for recursively deleting a file or directory.
 * @param path The path to the file or directory to delete.
 * @return 0 on success, -1 on failure.
 */
int FsDeleteRecursive(const char* path) {
    FsNode* node = FsFind(path);
    if (!node || !node->parent) {
        return -1; // Can't find node or trying to delete root
    }

    // Step 1: Unlink from parent's list
    UnlinkNode(node);

    // Step 2: Start the recursive deletion
    FsDeleteRecursiveI(node);

    return 0;
}


// --- Utility and Helper Functions ---

/**
 * @brief Finds the parent directory of a given path and extracts the child's name.
 * @param path The full path (e.g., "/System/Logs/boot.log").
 * @param child_name_out Output buffer to store the final component name (e.g., "boot.log").
 * @return A pointer to the parent FsNode (e.g., for "/System/Logs/"), or NULL on failure.
 */
static FsNode* FsFindParent(const char* path, char* child_name_out) {
    if (!path || path[0] != '/') return NULL;

    int len = StringLength(path);
    if (len == 0) return NULL;

    // Find the last '/'
    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }

    // Extract child name
    const char* name_start = path + last_slash + 1;
    if (*name_start == '\0') return NULL; // Path ends in a slash like /a/b/
    FastStrCopy(child_name_out, name_start, MAX_FILENAME - 1);
    child_name_out[MAX_FILENAME - 1] = '\0';

    // Extract parent path
    char parent_path[MAX_PATH];
    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        int parent_len = last_slash;
        FastMemcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
    }

    return FsFind(parent_path);
}

FsNode* FsReaddir(const char* path) {
    FsNode* dir_node = FsFind(path);
    if (!dir_node || dir_node->type != FS_DIRECTORY) return NULL;
    return dir_node->children;
}

int FsListDir(const char* path) {
    FsNode* dir_node = FsFind(path);
    if (!dir_node || dir_node->type != FS_DIRECTORY) return -1;

    FsNode* child = dir_node->children;
    if (!child) {
        return 0;
    }

    while (child) {
        PrintKernel(child->type == FS_DIRECTORY ? "[DIR]  " : "[FILE] ");
        PrintKernel(child->name);
        PrintKernel("\n");
        child = child->next_sibling;
    }

    return 0;
}

// --- High-Level Wrappers ---

int FsCreateFile(const char* path) {
    int fd = FsOpen(path, FS_WRITE);
    if (fd < 0) return -1;
    FsClose(fd);
    return 0;
}

int FsWriteFile(const char* path, const void* buffer, size_t size) {
    int fd = FsOpen(path, FS_WRITE | FS_APPEND);
    if (fd < 0) return -1;
    int result = FsWrite(fd, buffer, size);
    FsClose(fd);
    return result;
}