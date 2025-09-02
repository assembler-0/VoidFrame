#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "../mm/MemOps.h"
#include "StringOps.h"
#define MAX_PATH_COMPONENTS 32

/**
 * Overly complex sh*t
 * @brief Resolves a path, handling '.' and '..', to its canonical form.
 *
 * @param current_dir The current working directory (must be an absolute path).
 * @param input The path to resolve (can be absolute or relative).
 * @param output The buffer to store the resolved, absolute path.
 * @param max_len The size of the output buffer.
 */
static inline void __attribute__((always_inline)) ResolveSystemPath(const char* current_dir, const char* input, char* output, int max_len) {
    if (!input || !output || !current_dir) {
        if (output && max_len > 0) output[0] = '\0';
        return;
    }

    // Validate capacity: need at least room for "/" and NUL
    if (max_len <= 1) {
        if (output && max_len > 0) output[0] = '\0';
        return;
    }
    // A temporary buffer to hold the full, unprocessed path
    char full_path[max_len];

    // 1. Create the full, absolute path to be processed.
    if (input[0] == '/') {
        // Input is already an absolute path.
        FastStrCopy(full_path, input, max_len);
    } else {
        // Input is relative, so combine with the current directory.
        FastStrCopy(full_path, current_dir, max_len);
        int current_len = FastStrlen(full_path, max_len);

        // Add a separating slash if needed.
        if (current_len > 1 && full_path[current_len - 1] != '/') {
            if (current_len + 1 < max_len) {
                full_path[current_len++] = '/';
                full_path[current_len] = '\0';
            }
        } else if (current_len == 0) {
            // Defensive: ensure absolute root
            full_path[0] = '/';
            full_path[1] = '\0';
            current_len = 1;
        }
        // Append input within remaining capacity
        FastStrCopy(full_path + current_len, input, (int)(max_len - current_len));
    }

    // 2. Process the path using a stack-like approach for components.
    const char* components[MAX_PATH_COMPONENTS];
    int component_count = 0;

    char* path_copy = full_path;
    char* component_start = path_copy;

    // Skip leading slashes
    while (*component_start == '/') component_start++;

    while (*component_start != '\0') {
        char* component_end = component_start;
        while (*component_end != '\0' && *component_end != '/') {
            component_end++;
        }

        // Temporarily null-terminate the component to make comparisons easy
        char original_char = *component_end;
        *component_end = '\0';

        if (FastStrCmp(component_start, "..") == 0) {
            // ".." -> Pop from the stack
            if (component_count > 0) {
                component_count--;
            }
        } else if (FastStrCmp(component_start, ".") == 0) {
            // "." -> Do nothing
        } else {
            // Normal component -> Push onto the stack
            if (component_count < MAX_PATH_COMPONENTS) {
                components[component_count++] = component_start;
            }
        }

        // Restore the original character
        *component_end = original_char;

        // Move to the start of the next component
        component_start = component_end;
        while (*component_start == '/') component_start++;
    }

    // 3. Reconstruct the final, canonical path from the components.
    char* out_ptr = output;
    int remaining_len = max_len;

    // Start with the root slash
    *out_ptr++ = '/';
    remaining_len--;

    if (component_count == 0) {
        // The path resolved to the root directory itself.
        *out_ptr = '\0';
        return;
    }

    for (int i = 0; i < component_count; i++) {
        int comp_len = FastStrlen(components[i], remaining_len);
        if (comp_len >= remaining_len - 1) { // -1 for slash/null
            break; // Not enough space
        }

        FastMemcpy(out_ptr, components[i], comp_len);
        out_ptr += comp_len;
        remaining_len -= comp_len;

        // Add a slash if it's not the last component
        if (i < component_count - 1 && remaining_len > 1) {
            *out_ptr++ = '/';
            remaining_len--;
        }
    }

    *out_ptr = '\0';
}

/**
 * @brief Simple and working path resolver
 *
 * @param current_dir The current working directory
 * @param input The path to resolve (can be absolute or relative)
 * @param output Buffer to store the resolved path
 * @param max_len Size of the output buffer
 */
static inline  void __attribute__((always_inline)) ResolvePathS(const char* current_dir, const char* input, char* output, int max_len) {
    // Avoid OOB and ensure output is always valid
    if (!output || max_len <= 0) return;
    output[0] = '\0';
    if (!input) return;
    if (!current_dir) current_dir = "/";
    if (input[0] == '/') {
        // Absolute path
        FastStrCopy(output, input, max_len);
    } else {
        // Relative path - combine with current directory
        int curr_len = 0;
        while (current_dir[curr_len] && curr_len < max_len - 1) {
            output[curr_len] = current_dir[curr_len];
            curr_len++;
        }
        if (curr_len > 0 && current_dir[curr_len - 1] != '/' && curr_len < max_len - 1) {
            output[curr_len++] = '/';
        }
        int input_len = 0;
        while (input[input_len] && curr_len + input_len < max_len - 1) {
            output[curr_len + input_len] = input[input_len];
            input_len++;
        }
        output[curr_len + input_len] = '\0';
    }
}

/**
 * @brief Hybrid path resolver - reliable and functional
 * Handles .. and . components without complex stack operations
 *
 * @param current_dir The current working directory
 * @param input The path to resolve (can be absolute or relative)
 * @param output Buffer to store the resolved path
 * @param max_len Size of the output buffer
 */
static inline void __attribute__((always_inline)) ResolvePathH(const char* current_dir, const char* input, char* output, int max_len) {
    if (!input || !output || max_len <= 1) {
        if (output && max_len > 0) output[0] = '\0';
        return;
    }

    // Use fixed-size buffer instead of VLA
    static char work_buffer[512];
    int work_len = 0;

    // Step 1: Create the full path to sched
    if (input[0] == '/') {
        // Absolute path - copy input directly
        int i = 0;
        while (input[i] && i < sizeof(work_buffer) - 1) {
            work_buffer[i] = input[i];
            i++;
        }
        work_buffer[i] = '\0';
        work_len = i;
    } else {
        // Relative path - combine with current directory
        int i = 0;
        // Copy current directory
        while (current_dir && current_dir[i] && i < sizeof(work_buffer) - 2) {
            work_buffer[i] = current_dir[i];
            i++;
        }

        // Add separator if needed
        if (i > 0 && work_buffer[i-1] != '/') {
            work_buffer[i++] = '/';
        }

        // Append input path
        int j = 0;
        while (input[j] && i < sizeof(work_buffer) - 1) {
            work_buffer[i++] = input[j++];
        }
        work_buffer[i] = '\0';
        work_len = i;
    }

    // Step 2: Simple in-place normalization of .. and .
    char* read_ptr = work_buffer;
    char* write_ptr = work_buffer;

    // Always start with root
    *write_ptr++ = '/';

    // Skip initial slashes
    while (*read_ptr == '/') read_ptr++;

    char component[64];
    int comp_idx = 0;

    while (*read_ptr) {
        if (*read_ptr == '/' || *read_ptr == '\0') {
            // End of component
            component[comp_idx] = '\0';

            if (comp_idx == 0) {
                // Empty component (double slash) - skip
            } else if (comp_idx == 1 && component[0] == '.') {
                // Current directory - skip
            } else if (comp_idx == 2 && component[0] == '.' && component[1] == '.') {
                // Parent directory - back up
                if (write_ptr > work_buffer + 1) {
                    write_ptr--; // Skip the trailing slash
                    while (write_ptr > work_buffer + 1 && *(write_ptr-1) != '/') {
                        write_ptr--;
                    }
                }
            } else {
                // Normal component - add it
                for (int i = 0; i < comp_idx; i++) {
                    *write_ptr++ = component[i];
                }
                *write_ptr++ = '/';
            }

            comp_idx = 0;
            while (*read_ptr == '/') read_ptr++; // Skip multiple slashes
        } else {
            // Build component
            if (comp_idx < sizeof(component) - 1) {
                component[comp_idx++] = *read_ptr;
            }
            read_ptr++;
        }
    }

    // Handle final component if exists
    if (comp_idx > 0) {
        component[comp_idx] = '\0';
        if (comp_idx == 1 && component[0] == '.') {
            // Current directory - skip
        } else if (comp_idx == 2 && component[0] == '.' && component[1] == '.') {
            // Parent directory - back up
            if (write_ptr > work_buffer + 1) {
                write_ptr--; // Skip the trailing slash
                while (write_ptr > work_buffer + 1 && *(write_ptr-1) != '/') {
                    write_ptr--;
                }
            }
        } else {
            // Normal component - add it
            for (int i = 0; i < comp_idx; i++) {
                *write_ptr++ = component[i];
            }
        }
    }

    // Remove trailing slash unless it's root
    if (write_ptr > work_buffer + 1 && *(write_ptr-1) == '/') {
        write_ptr--;
    }

    // Ensure we end with null terminator
    *write_ptr = '\0';

    // Step 3: Copy result to output buffer
    int result_len = write_ptr - work_buffer;
    int copy_len = (result_len < max_len - 1) ? result_len : max_len - 1;

    for (int i = 0; i < copy_len; i++) {
        output[i] = work_buffer[i];
    }
    output[copy_len] = '\0';
}

#endif