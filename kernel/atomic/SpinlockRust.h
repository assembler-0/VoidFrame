#ifndef RUST_SPINLOCK_H
#define RUST_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Opaque types for Rust structures
    typedef struct RustSpinLock RustSpinLock;
    typedef struct RustMcsLock RustMcsLock;
    typedef struct RustMcsNode RustMcsNode;
    typedef struct RustRwLock RustRwLock;

    // SpinLock functions
    RustSpinLock* rust_spinlock_new(void);
    void rust_spinlock_free(RustSpinLock* lock);
    void rust_spinlock_lock(RustSpinLock* lock);
    void rust_spinlock_unlock(RustSpinLock* lock);
    bool rust_spinlock_try_lock(RustSpinLock* lock);

    // IRQ-safe SpinLock functions
    uint64_t rust_spinlock_lock_irqsave(RustSpinLock* lock);
    void rust_spinlock_unlock_irqrestore(RustSpinLock* lock, uint64_t flags);

    // MCS Lock functions
    RustMcsLock* rust_mcs_lock_new(void);
    void rust_mcs_lock_free(RustMcsLock* lock);
    RustMcsNode* rust_mcs_node_new(void);
    void rust_mcs_node_free(RustMcsNode* node);
    void rust_mcs_lock(RustMcsLock* lock, RustMcsNode* node);
    void rust_mcs_unlock(RustMcsLock* lock, RustMcsNode* node);

    // RwLock functions
    RustRwLock* rust_rwlock_new(void);
    void rust_rwlock_free(RustRwLock* lock);
    void rust_rwlock_read_lock(RustRwLock* lock, uint32_t owner_id);
    void rust_rwlock_read_unlock(RustRwLock* lock, uint32_t owner_id);
    void rust_rwlock_write_lock(RustRwLock* lock, uint32_t owner_id);
    void rust_rwlock_write_unlock(RustRwLock* lock);

#ifdef __cplusplus
}
#endif

#endif // RUST_SPINLOCK_H