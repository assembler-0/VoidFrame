use core::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use core::arch::x86_64::{_rdtsc, _mm_pause};
use core::cell::UnsafeCell;
use core::marker::PhantomData;
use core::ops::{Deref, DerefMut};

const DEADLOCK_TIMEOUT_CYCLES: u64 = 100_000_000;
const MAX_BACKOFF_CYCLES: u64 = 1024;
const CONTENTION_THRESHOLD: u32 = 16;
const YIELD_THRESHOLD: u32 = 64;

#[inline(always)]
fn rdtsc() -> u64 {
    unsafe { _rdtsc() }
}

#[inline(always)]
fn pause() {
    unsafe { _mm_pause() }
}

#[inline(always)]
fn backoff_delay(cycles: u64) {
    let start = rdtsc();
    while rdtsc() - start < cycles {
        pause();
    }
}

extern "C" {
    fn lapic_get_id() -> u32;
}


#[repr(C)]
pub struct SpinLock {
    locked: AtomicBool,
    contention_count: AtomicU32,
    owner_cpu: AtomicU32,
    acquire_time: AtomicU64,
}

// RAII guard for automatic unlock
pub struct SpinLockGuard<'a, T> {
    lock: &'a SpinLock,
    data: &'a mut T,
    _phantom: PhantomData<*const ()>, // !Send + !Sync
}

// Advanced spinlock with data protection
#[repr(C)]
pub struct SpinMutex<T> {
    lock: SpinLock,
    data: UnsafeCell<T>,
}

unsafe impl<T: Send> Sync for SpinMutex<T> {}
unsafe impl<T: Send> Send for SpinMutex<T> {}

impl SpinLock {
    pub const fn new() -> Self {
        Self {
            locked: AtomicBool::new(false),
            contention_count: AtomicU32::new(0),
            owner_cpu: AtomicU32::new(u32::MAX),
            acquire_time: AtomicU64::new(0),
        }
    }

    #[inline]
    pub fn lock(&self) {
        let start = rdtsc();
        let mut cpu_id = 0;
        unsafe {
            cpu_id = lapic_get_id();
        }
        let mut backoff = 1u64;
        let mut attempts = 0u32;
        let mut local_spins = 0u32;

        loop {
            // Fast path: try to acquire without contention
            if self.try_lock_fast(cpu_id, start) {
                return;
            }

            // Deadlock detection with owner tracking
            if rdtsc() - start > DEADLOCK_TIMEOUT_CYCLES {
                self.handle_potential_deadlock(cpu_id, start);
                continue;
            }

            attempts += 1;
            local_spins += 1;

            // Adaptive strategy based on contention level
            let contention = self.contention_count.load(Ordering::Relaxed);
            
            if contention < CONTENTION_THRESHOLD {
                // Low contention: aggressive spinning
                self.spin_wait_adaptive(local_spins);
            } else if attempts < YIELD_THRESHOLD {
                // Medium contention: exponential backoff
                backoff_delay(backoff);
                backoff = (backoff * 2).min(MAX_BACKOFF_CYCLES);
            } else {
                // High contention: yield to scheduler (kernel would implement this)
                backoff_delay(MAX_BACKOFF_CYCLES);
                local_spins = 0;
            }
        }
    }

    #[inline]
    fn try_lock_fast(&self, cpu_id: u32, start_time: u64) -> bool {
        if !self.locked.load(Ordering::Relaxed) 
            && !self.locked.swap(true, Ordering::Acquire) {
            self.owner_cpu.store(cpu_id, Ordering::Relaxed);
            self.acquire_time.store(start_time, Ordering::Relaxed);
            return true;
        }
        false
    }

    #[inline]
    fn spin_wait_adaptive(&self, local_spins: u32) {
        let spin_count = if local_spins < 32 { 4 } else { 16 };
        for _ in 0..spin_count {
            if !self.locked.load(Ordering::Relaxed) {
                break;
            }
            pause();
        }
    }

    #[inline]
    fn handle_potential_deadlock(&self, cpu_id: u32, start_time: u64) {
        let owner = self.owner_cpu.load(Ordering::Relaxed);
        let owner_time = self.acquire_time.load(Ordering::Relaxed);
        
        // In a real kernel, this would check if owner CPU is still alive
        // and potentially break the lock or log the deadlock
        if owner == cpu_id {
            // Self-deadlock detected - this should never happen
            return;
        }
        
        // Force yield and reset timing
        backoff_delay(MAX_BACKOFF_CYCLES);
        self.contention_count.fetch_add(1, Ordering::Relaxed);
    }

    #[inline]
    pub fn unlock(&self) {
        self.owner_cpu.store(u32::MAX, Ordering::Relaxed);
        self.acquire_time.store(0, Ordering::Relaxed);
        self.locked.store(false, Ordering::Release);
        
        // Decay contention counter over time
        let contention = self.contention_count.load(Ordering::Relaxed);
        if contention > 0 {
            self.contention_count.store(contention.saturating_sub(1), Ordering::Relaxed);
        }
    }

    #[inline]
    pub fn try_lock(&self) -> bool {
        if !self.locked.swap(true, Ordering::Acquire) {
            let mut cpu_id = 0;
            unsafe {
                cpu_id = lapic_get_id();
            }
            self.owner_cpu.store(cpu_id, Ordering::Relaxed);
            self.acquire_time.store(rdtsc(), Ordering::Relaxed);
            true
        } else {
            false
        }
    }

    #[inline]
    pub fn is_locked(&self) -> bool {
        self.locked.load(Ordering::Relaxed)
    }

    #[inline]
    pub fn contention_level(&self) -> u32 {
        self.contention_count.load(Ordering::Relaxed)
    }
}

// RAII Guard implementation
impl<'a, T> SpinLockGuard<'a, T> {
    fn new(lock: &'a SpinLock, data: &'a mut T) -> Self {
        Self {
            lock,
            data,
            _phantom: PhantomData,
        }
    }
}

impl<'a, T> Drop for SpinLockGuard<'a, T> {
    fn drop(&mut self) {
        self.lock.unlock();
    }
}

impl<'a, T> Deref for SpinLockGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &T {
        self.data
    }
}

impl<'a, T> DerefMut for SpinLockGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        self.data
    }
}

// SpinMutex implementation
impl<T> SpinMutex<T> {
    pub const fn new(data: T) -> Self {
        Self {
            lock: SpinLock::new(),
            data: UnsafeCell::new(data),
        }
    }

    pub fn lock(&'_ self) -> SpinLockGuard<'_, T> {
        self.lock.lock();
        SpinLockGuard::new(&self.lock, unsafe { &mut *self.data.get() })
    }

    pub fn try_lock(&'_ self) -> Option<SpinLockGuard<'_, T>> {
        if self.lock.try_lock() {
            Some(SpinLockGuard::new(&self.lock, unsafe { &mut *self.data.get() }))
        } else {
            None
        }
    }

    pub fn is_locked(&self) -> bool {
        self.lock.is_locked()
    }

    pub fn contention_level(&self) -> u32 {
        self.lock.contention_level()
    }

    pub fn into_inner(self) -> T {
        self.data.into_inner()
    }
}