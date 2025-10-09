use core::sync::atomic::{AtomicPtr, AtomicBool, Ordering};
use core::ptr;

#[repr(C)]
pub struct McsNode {
    next: AtomicPtr<McsNode>,
    locked: AtomicBool,
}

impl McsNode {
    pub const fn new() -> Self {
        Self {
            next: AtomicPtr::new(ptr::null_mut()),
            locked: AtomicBool::new(false),
        }
    }
}

#[repr(C)]
pub struct McsLock {
    tail: AtomicPtr<McsNode>,
}

impl McsLock {
    pub const fn new() -> Self {
        Self {
            tail: AtomicPtr::new(ptr::null_mut()),
        }
    }

    #[inline]
    pub fn lock(&self, node: &mut McsNode) {
        node.next.store(ptr::null_mut(), Ordering::Relaxed);
        node.locked.store(true, Ordering::Relaxed);

        let prev = self.tail.swap(node as *mut McsNode, Ordering::AcqRel);
        
        if !prev.is_null() {
            unsafe {
                (*prev).next.store(node as *mut McsNode, Ordering::Release);
            }
            while node.locked.load(Ordering::Acquire) {
                unsafe { core::arch::x86_64::_mm_pause() };
            }
        }
    }

    #[inline]
    pub fn unlock(&self, node: &mut McsNode) {
        let next = node.next.load(Ordering::Acquire);
        
        if next.is_null() {
            if self.tail.compare_exchange(
                node as *mut McsNode,
                ptr::null_mut(),
                Ordering::Release,
                Ordering::Relaxed
            ).is_ok() {
                return;
            }
            
            while node.next.load(Ordering::Acquire).is_null() {
                unsafe { core::arch::x86_64::_mm_pause() };
            }
        }
        
        let next = node.next.load(Ordering::Acquire);
        if !next.is_null() {
            unsafe {
                (*next).locked.store(false, Ordering::Release);
            }
        }
    }
}