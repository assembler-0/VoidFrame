use crate::{SpinLock, McsLock, McsNode, RwLock};
use core::panic::{PanicInfo, PanicMessage};

// External C functions from Io.h
extern "C" {
    fn save_irq_flags() -> u64;
    fn restore_irq_flags(flags: u64);
    fn cli();
    pub(crate) fn Yield(); // Declare the C function for yielding
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    extern "C" {
        fn Panic(msg: *const u8) -> !;
    }
    unsafe {
        Panic("=>$s\0".as_ptr() as *const u8);
    }
}

// Static storage for locks (kernel will manage allocation)
static mut SPINLOCK_STORAGE: [SpinLock; 256] = [const { SpinLock::new() }; 256];
static mut SPINLOCK_USED: [bool; 256] = [false; 256];

#[no_mangle]
pub extern "C" fn rust_spinlock_new() -> *mut SpinLock {
    unsafe {
        for i in 0..256 {
            if !SPINLOCK_USED[i] {
                SPINLOCK_USED[i] = true;
                return &mut SPINLOCK_STORAGE[i] as *mut SpinLock;
            }
        }
    }
    core::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_spinlock_free(lock: *mut SpinLock) {
    if !lock.is_null() {
        unsafe {
            for i in 0..64 {
                if &mut SPINLOCK_STORAGE[i] as *mut SpinLock == lock {
                    SPINLOCK_USED[i] = false;
                    break;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_lock(lock: *mut SpinLock) {
    if !lock.is_null() {
        unsafe { (*lock).lock() };
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_unlock(lock: *mut SpinLock) {
    if !lock.is_null() {
        unsafe { (*lock).unlock() };
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_try_lock(lock: *mut SpinLock) -> bool {
    if !lock.is_null() {
        unsafe { (*lock).try_lock() }
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_contention_level(lock: *mut SpinLock) -> u32 {
    if !lock.is_null() {
        unsafe { (*lock).contention_level() }
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_lock_order(lock: *mut SpinLock) -> u32 {
    if !lock.is_null() {
        unsafe { (*lock).lock_order() }
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_owner_cpu(lock: *mut SpinLock) -> u32 {
    if !lock.is_null() {
        unsafe { (*lock).owner_cpu() }
    } else {
        u32::MAX
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_new_with_order(order: u32) -> *mut SpinLock {
    unsafe {
        for i in 0..256 {
            if !SPINLOCK_USED[i] {
                SPINLOCK_USED[i] = true;
                SPINLOCK_STORAGE[i] = SpinLock::new_with_order(order);
                return &mut SPINLOCK_STORAGE[i] as *mut SpinLock;
            }
        }
    }
    core::ptr::null_mut()
}

// Static storage for MCS locks and nodes
static mut MCS_LOCK_STORAGE: [McsLock; 128] = [const { McsLock::new() }; 128];
static mut MCS_LOCK_USED: [bool; 128] = [false; 128];
static mut MCS_NODE_STORAGE: [McsNode; 512] = [const { McsNode::new() }; 512];
static mut MCS_NODE_USED: [bool; 512] = [false; 512];

#[no_mangle]
pub extern "C" fn rust_mcs_lock_new() -> *mut McsLock {
    unsafe {
        for i in 0..128 {
            if !MCS_LOCK_USED[i] {
                MCS_LOCK_USED[i] = true;
                return &mut MCS_LOCK_STORAGE[i] as *mut McsLock;
            }
        }
    }
    core::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_mcs_lock_free(lock: *mut McsLock) {
    if !lock.is_null() {
        unsafe {
            for i in 0..32 {
                if &mut MCS_LOCK_STORAGE[i] as *mut McsLock == lock {
                    MCS_LOCK_USED[i] = false;
                    break;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_mcs_node_new() -> *mut McsNode {
    unsafe {
        for i in 0..512 {
            if !MCS_NODE_USED[i] {
                MCS_NODE_USED[i] = true;
                return &mut MCS_NODE_STORAGE[i] as *mut McsNode;
            }
        }
    }
    core::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_mcs_node_free(node: *mut McsNode) {
    if !node.is_null() {
        unsafe {
            for i in 0..128 {
                if &mut MCS_NODE_STORAGE[i] as *mut McsNode == node {
                    MCS_NODE_USED[i] = false;
                    break;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_mcs_lock(lock: *mut McsLock, node: *mut McsNode) {
    if !lock.is_null() && !node.is_null() {
        unsafe { (*lock).lock(&mut *node) };
    }
}

#[no_mangle]
pub extern "C" fn rust_mcs_unlock(lock: *mut McsLock, node: *mut McsNode) {
    if !lock.is_null() && !node.is_null() {
        unsafe { (*lock).unlock(&mut *node) };
    }
}

#[no_mangle]
pub extern "C" fn rust_mcs_try_lock(lock: *mut McsLock, node: *mut McsNode) -> bool {
    if !lock.is_null() && !node.is_null() {
        unsafe { (*lock).try_lock(&mut *node) }
    } else {
        false
    }
}

// Static storage for RwLocks
static mut RWLOCK_STORAGE: [RwLock; 32] = [const { RwLock::new() }; 32];
static mut RWLOCK_USED: [bool; 32] = [false; 32];

#[no_mangle]
pub extern "C" fn rust_rwlock_new() -> *mut RwLock {
    unsafe {
        for i in 0..32 {
            if !RWLOCK_USED[i] {
                RWLOCK_USED[i] = true;
                return &mut RWLOCK_STORAGE[i] as *mut RwLock;
            }
        }
    }
    core::ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn rust_rwlock_free(lock: *mut RwLock) {
    if !lock.is_null() {
        unsafe {
            for i in 0..32 {
                if &mut RWLOCK_STORAGE[i] as *mut RwLock == lock {
                    RWLOCK_USED[i] = false;
                    break;
                }
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_rwlock_read_lock(lock: *mut RwLock, owner_id: u32) {
    if !lock.is_null() {
        unsafe { (*lock).read_lock(owner_id) };
    }
}

#[no_mangle]
pub extern "C" fn rust_rwlock_read_unlock(lock: *mut RwLock, owner_id: u32) {
    if !lock.is_null() {
        unsafe { (*lock).read_unlock(owner_id) };
    }
}

#[no_mangle]
pub extern "C" fn rust_rwlock_write_lock(lock: *mut RwLock, owner_id: u32) {
    if !lock.is_null() {
        unsafe { (*lock).write_lock(owner_id) };
    }
}

#[no_mangle]
pub extern "C" fn rust_rwlock_write_unlock(lock: *mut RwLock) {
    if !lock.is_null() {
        unsafe { (*lock).write_unlock() };
    }
}

// IRQ-safe spinlock functions
#[no_mangle]
pub extern "C" fn rust_spinlock_lock_irqsave(lock: *mut SpinLock) -> u64 {
    if !lock.is_null() {
        unsafe {
            let flags = save_irq_flags();
            cli();
            (*lock).lock();
            flags
        }
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn rust_spinlock_unlock_irqrestore(lock: *mut SpinLock, flags: u64) {
    if !lock.is_null() {
        unsafe {
            (*lock).unlock();
            restore_irq_flags(flags);
        }
    }
}