use core::sync::atomic::{AtomicPtr, AtomicBool, Ordering};
use core::arch::x86_64::_mm_pause;
use core::cell::UnsafeCell;
use core::marker::PhantomData;
use core::ops::{Deref, DerefMut};
use core::ptr;

#[inline(always)]
fn pause() {
    unsafe { _mm_pause() }
}

// MCS queue node - each thread has its own
#[repr(C, align(64))] // Cache line aligned
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

// MCS Lock - fair, NUMA-friendly
#[repr(C)]
pub struct McsLock {
    tail: AtomicPtr<McsNode>,
}

pub struct McsGuard<'a, T> {
    lock: &'a McsLock,
    node: &'a mut McsNode,
    data: &'a mut T,
    _phantom: PhantomData<*const ()>,
}

#[repr(C)]
pub struct McsMutex<T> {
    lock: McsLock,
    data: UnsafeCell<T>,
}

unsafe impl<T: Send> Sync for McsMutex<T> {}
unsafe impl<T: Send> Send for McsMutex<T> {}

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

        let prev = self.tail.swap(node, Ordering::AcqRel);
        
        if !prev.is_null() {
            // Queue not empty, link to predecessor
            unsafe {
                (*prev).next.store(node, Ordering::Release);
            }
            
            // Spin on local variable (NUMA-friendly)
            while node.locked.load(Ordering::Acquire) {
                pause();
            }
        }
        // If prev was null, we got the lock immediately
    }

    #[inline]
    pub fn unlock(&self, node: &mut McsNode) {
        let next = node.next.load(Ordering::Acquire);
        
        if next.is_null() {
            // Try to remove ourselves from tail
            if self.tail.compare_exchange_weak(
                node, 
                ptr::null_mut(), 
                Ordering::Release, 
                Ordering::Relaxed
            ).is_ok() {
                return; // Successfully removed, no successor
            }
            
            // Someone added themselves, wait for next pointer
            while node.next.load(Ordering::Acquire).is_null() {
                pause();
            }
        }
        
        // Pass lock to next waiter
        let next = node.next.load(Ordering::Acquire);
        if !next.is_null() {
            unsafe {
                (*next).locked.store(false, Ordering::Release);
            }
        }
    }

    #[inline]
    pub fn try_lock(&self, node: &mut McsNode) -> bool {
        node.next.store(ptr::null_mut(), Ordering::Relaxed);
        node.locked.store(false, Ordering::Relaxed);
        
        self.tail.compare_exchange_weak(
            ptr::null_mut(),
            node,
            Ordering::AcqRel,
            Ordering::Relaxed
        ).is_ok()
    }
}

impl<'a, T> McsGuard<'a, T> {
    fn new(lock: &'a McsLock, node: &'a mut McsNode, data: &'a mut T) -> Self {
        Self {
            lock,
            node,
            data,
            _phantom: PhantomData,
        }
    }
}

impl<'a, T> Drop for McsGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.unlock(self.node);
    }
}

impl<'a, T> Deref for McsGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.data
    }
}

impl<'a, T> DerefMut for McsGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.data
    }
}

impl<T> McsMutex<T> {
    pub const fn new(data: T) -> Self {
        Self {
            lock: McsLock::new(),
            data: UnsafeCell::new(data),
        }
    }

    pub fn lock<'a>(&'a self, node: &'a mut McsNode) -> McsGuard<'a, T> {
        self.lock.lock(node);
        McsGuard::new(&self.lock, node, unsafe { &mut *self.data.get() })
    }

    pub fn try_lock<'a>(&'a self, node: &'a mut McsNode) -> Option<McsGuard<'a, T>> {
        if self.lock.try_lock(node) {
            Some(McsGuard::new(&self.lock, node, unsafe { &mut *self.data.get() }))
        } else {
            None
        }
    }

    pub fn into_inner(self) -> T {
        self.data.into_inner()
    }
}