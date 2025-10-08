use core::sync::atomic::{AtomicBool, Ordering};
use core::arch::x86_64::{_rdtsc, _mm_pause};

const DEADLOCK_TIMEOUT_CYCLES: u64 = 100_000_000;
const MAX_BACKOFF_CYCLES: u64 = 1024;

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

#[repr(C)]
pub struct SpinLock {
    locked: AtomicBool,
}

impl SpinLock {
    pub const fn new() -> Self {
        Self {
            locked: AtomicBool::new(false),
        }
    }

    #[inline]
    pub fn lock(&self) {
        let start = rdtsc();
        let mut backoff = 1u64;
        let mut attempts = 0u32;

        loop {
            // Try to acquire without contention first
            if !self.locked.load(Ordering::Relaxed) 
                && !self.locked.swap(true, Ordering::Acquire) {
                return;
            }

            // Deadlock detection
            if rdtsc() - start > DEADLOCK_TIMEOUT_CYCLES {
                backoff_delay(MAX_BACKOFF_CYCLES);
                continue;
            }

            attempts += 1;

            // Adaptive spinning strategy
            if attempts < 100 {
                // Initial fast spinning with pause
                for _ in 0..64 {
                    if !self.locked.load(Ordering::Relaxed) {
                        break;
                    }
                    pause();
                }
            } else {
                // Switch to exponential backoff after many attempts
                backoff_delay(backoff);
                backoff = if backoff * 2 > MAX_BACKOFF_CYCLES {
                    MAX_BACKOFF_CYCLES
                } else {
                    backoff * 2
                };
            }
        }
    }

    #[inline]
    pub fn unlock(&self) {
        self.locked.store(false, Ordering::Release);
    }

    #[inline]
    pub fn try_lock(&self) -> bool {
        !self.locked.swap(true, Ordering::Acquire)
    }
}