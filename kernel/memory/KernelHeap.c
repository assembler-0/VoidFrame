#include "KernelHeap.h"
#include "VMem.h"
#include "Kernel.h"
#include "Spinlock.h"
#include "MemOps.h"
#include "Panic.h"

// Simple page-based allocator for now.
// A more sophisticated allocator (e.g., buddy system, slab allocator)
// would be needed for finer-grained allocations.

// Structure to store metadata for each allocated block
typedef struct HeapBlockHeader {
    size_t size; // Size of the user-requested data (not including header)
} HeapBlockHeader;

static volatile int kheap_lock = 0;

void KernelHeapInit() {
    // No specific initialization needed for this simple page-based allocator
    // VMemInit handles the underlying virtual memory setup.
    PrintKernelSuccess("[SYSTEM] Kernel Heap Initialized (page-based).\n");
}

void* KernelMemoryAlloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    // Calculate total size needed: user data + header
    size_t total_alloc_size = size + sizeof(HeapBlockHeader);

    // Allocate memory using VMemAlloc, which handles page alignment and mapping
    void* allocated_ptr = VMemAlloc(total_alloc_size);

    if (allocated_ptr == NULL) {
        PrintKernelError("[SYSTEM] KernelMemoryAlloc: Failed to allocate ");
        PrintKernelInt(size);
        PrintKernel(" bytes.\n");
        SpinUnlockIrqRestore(&kheap_lock, flags);
        return NULL;
    }

    // Store the size in the header
    HeapBlockHeader* header = (HeapBlockHeader*)allocated_ptr;
    header->size = size;

    // Return pointer to the user-accessible memory (after the header)
    void* user_ptr = (void*)((uint8_t*)allocated_ptr + sizeof(HeapBlockHeader));

    SpinUnlockIrqRestore(&kheap_lock, flags);
    return user_ptr;
}

void* KernelCallLocation(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = KernelMemoryAlloc(total_size);
    if (ptr) {
        FastMemset(ptr, 0, total_size);
    }
    return ptr;
}

void* KernelRealLocation(void* ptr, size_t size) {
    if (ptr == NULL) {
        return KernelMemoryAlloc(size);
    }
    if (size == 0) {
        KernelFree(ptr);
        return NULL;
    }

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);
    HeapBlockHeader* old_header = (HeapBlockHeader*)((uint8_t*)ptr - sizeof(HeapBlockHeader));
    size_t old_size = old_header->size;
    SpinUnlockIrqRestore(&kheap_lock, flags);

    void* new_ptr = KernelMemoryAlloc(size);
    if (!new_ptr) {
        PrintKernelError("[SYSTEM] KernelRealLocation: Failed to reallocate ");
        PrintKernelInt(size);
        PrintKernel(" bytes.\n");
        return NULL;
    }

    FastMemcpy(new_ptr, ptr, (old_size < size) ? old_size : size);

    KernelFree(ptr);

    return new_ptr;
}

void KernelFree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    // Get the header by subtracting the header size from the user pointer
    HeapBlockHeader* header = (HeapBlockHeader*)((uint8_t*)ptr - sizeof(HeapBlockHeader));

    // Get the original allocated size (including header)
    size_t original_alloc_size = header->size + sizeof(HeapBlockHeader);

    // Free the entire allocated block (including header) using VMemFree
    VMemFree((void*)header, original_alloc_size);

    SpinUnlockIrqRestore(&kheap_lock, flags);
}