# VoidFrame Rust Spinlock Suite

A complete, high-performance spinlock implementation in Rust with deadlock detection and C FFI bindings.

## Features

- **Advanced SpinLock**: Adaptive spinning with exponential backoff and deadlock detection
- **MCS Lock**: Fair queuing lock with reduced cache bouncing
- **Reader-Writer Lock**: Supports multiple readers or single writer with recursive write locking
- **C FFI**: Complete C bindings for seamless integration with kernel code
- **No-std**: Designed for kernel/bare-metal environments
- **Thread-safe**: All implementations use atomic operations

## Performance Features

- **Deadlock Detection**: Automatic timeout and recovery after 100M cycles
- **Adaptive Backoff**: Exponential backoff strategy to reduce CPU usage
- **Cache-friendly**: MCS locks minimize cache line bouncing
- **PAUSE instruction**: Uses x86 PAUSE for efficient spinning

## Usage from C

```c
#include <SpinlockRust.h>

// Basic spinlock
RustSpinLock* lock = rust_spinlock_new();
rust_spinlock_lock(lock);
// Critical section
rust_spinlock_unlock(lock);
rust_spinlock_free(lock);

// MCS lock (fair)
RustMcsLock* mcs_lock = rust_mcs_lock_new();
RustMcsNode* node = rust_mcs_node_new();
rust_mcs_lock(mcs_lock, node);
// Critical section
rust_mcs_unlock(mcs_lock, node);
rust_mcs_node_free(node);
rust_mcs_lock_free(mcs_lock);

// Reader-writer lock
RustRwLock* rw_lock = rust_rwlock_new();
rust_rwlock_read_lock(rw_lock, process_id);
// Read critical section
rust_rwlock_read_unlock(rw_lock, process_id);
rust_rwlock_free(rw_lock);
```

## Building

```bash
cd kernel/atomic/rust
cargo build --release
```

The compiled library will be at `target/x86_64-unknown-none/release/libvoidframe_spinlock.a`
## Architecture

- **Static Allocation**: Uses pre-allocated static arrays (no heap allocation)
- **Lock-free Operations**: All synchronization uses atomic operations
- **Panic Handler**: Includes minimal panic handler for no-std environment
- **x86_64 Optimized**: Uses RDTSC and PAUSE instructions for optimal performance