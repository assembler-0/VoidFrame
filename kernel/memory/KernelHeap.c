#include "KernelHeap.h"
#include "VMem.h"
#include "Kernel.h"
#include "Spinlock.h"
#include "MemOps.h"

typedef struct HeapBlockHeader {
    size_t size; // Size of the user-requested data (not including header)
} HeapBlockHeader;

static volatile int kheap_lock = 0;

void KernelHeapInit() {

    PrintKernelSuccess("[SYSTEM] Kernel Heap Initialized.\n");
}

void* KernelMemoryAlloc(size_t size) {
    if (size == 0 || size > (1ULL << 32)) return NULL;

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    size_t total_alloc_size = size + sizeof(HeapBlockHeader);
    void* allocated_ptr = VMemAlloc(total_alloc_size);

    if (!allocated_ptr) {
        SpinUnlockIrqRestore(&kheap_lock, flags);
        return NULL;
    }

    HeapBlockHeader* header = (HeapBlockHeader*)allocated_ptr;
    header->size = size;

    void* user_ptr = (void*)((uint8_t*)allocated_ptr + sizeof(HeapBlockHeader));
    SpinUnlockIrqRestore(&kheap_lock, flags);
    return user_ptr;
}

void* KernelCallocate(size_t num, size_t size) {
    size_t total_size = num * size;
    void* ptr = KernelMemoryAlloc(total_size);
    if (ptr) {
        FastMemset(ptr, 0, total_size);
    }
    return ptr;
}

void* KernelReallocate(void* ptr, size_t size) {
    if (!ptr) return KernelMemoryAlloc(size);
    if (size == 0) {
        KernelFree(ptr);
        return NULL;
    }
    if (size > (1ULL << 32)) return NULL;

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);
    HeapBlockHeader* old_header = (HeapBlockHeader*)((uint8_t*)ptr - sizeof(HeapBlockHeader));
    size_t old_size = old_header->size;
    SpinUnlockIrqRestore(&kheap_lock, flags);

    void* new_ptr = KernelMemoryAlloc(size);
    if (!new_ptr) return NULL;

    FastMemcpy(new_ptr, ptr, (old_size < size) ? old_size : size);
    KernelFree(ptr);
    return new_ptr;
}

void KernelFree(void* ptr) {
    if (!ptr) return;

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);
    HeapBlockHeader* header = (HeapBlockHeader*)((uint8_t*)ptr - sizeof(HeapBlockHeader));
    size_t user_size = header->size;
    size_t original_alloc_size = user_size + sizeof(HeapBlockHeader);
    
    // Zero user data for security
    FastMemset(ptr, 0, user_size);
    SpinUnlockIrqRestore(&kheap_lock, flags);

    VMemFree((void*)header, original_alloc_size);
}