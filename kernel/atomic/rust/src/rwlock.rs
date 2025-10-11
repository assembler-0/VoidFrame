use core::sync::atomic::{AtomicU32, AtomicBool, Ordering};

#[repr(C)]
pub struct RwLock {
    readers: AtomicU32,
    writer: AtomicBool,
    waiting_writers: AtomicU32,  // Track waiting writers for fairness
    owner: AtomicU32,
    recursion: AtomicU32,
}

impl RwLock {
    pub const fn new() -> Self {
        Self {
            readers: AtomicU32::new(0),
            writer: AtomicBool::new(false),
            waiting_writers: AtomicU32::new(0),
            owner: AtomicU32::new(0),
            recursion: AtomicU32::new(0),
        }
    }

    #[inline]
    pub fn read_lock(&self, owner_id: u32) {
        if self.writer.load(Ordering::Acquire) && self.owner.load(Ordering::Acquire) == owner_id {
            return;
        }

        let mut backoff = 1u32;
        loop {
            // Writer preference: don't acquire read lock if writers are waiting
            while self.writer.load(Ordering::Acquire) || self.waiting_writers.load(Ordering::Acquire) > 0 {
                self.spin_wait(&mut backoff);
            }
            
            // Atomic increment with check
            let prev_readers = self.readers.fetch_add(1, Ordering::AcqRel);
            
            // Double-check no writer acquired lock after we incremented
            if !self.writer.load(Ordering::Acquire) {
                break;
            }
            
            // Rollback and retry
            self.readers.fetch_sub(1, Ordering::AcqRel);
            backoff = 1; // Reset backoff after rollback
        }
    }

    #[inline]
    pub fn read_unlock(&self, owner_id: u32) {
        if self.writer.load(Ordering::Acquire) && self.owner.load(Ordering::Acquire) == owner_id {
            return;
        }
        self.readers.fetch_sub(1, Ordering::AcqRel);
    }

    #[inline]
    pub fn write_lock(&self, owner_id: u32) {
        if self.writer.load(Ordering::Acquire) && self.owner.load(Ordering::Acquire) == owner_id {
            self.recursion.fetch_add(1, Ordering::AcqRel);
            return;
        }

        // Signal that a writer is waiting
        self.waiting_writers.fetch_add(1, Ordering::AcqRel);
        
        let mut backoff = 1u32;
        
        // Try to acquire writer flag
        while self.writer.swap(true, Ordering::AcqRel) {
            self.spin_wait(&mut backoff);
        }

        // Wait for all readers to finish
        backoff = 1;
        while self.readers.load(Ordering::Acquire) > 0 {
            self.spin_wait(&mut backoff);
        }

        // We now have exclusive access
        self.waiting_writers.fetch_sub(1, Ordering::AcqRel);
        self.owner.store(owner_id, Ordering::Release);
        self.recursion.store(1, Ordering::Release);
    }

    #[inline]
    pub fn write_unlock(&self) {
        let recursion = self.recursion.load(Ordering::Acquire);
        
        if recursion <= 1 {
            self.recursion.store(0, Ordering::Release);
            self.owner.store(0, Ordering::Release);
            self.writer.store(false, Ordering::Release);
        } else {
            self.recursion.store(recursion - 1, Ordering::Release);
        }
    }

    #[inline]
    fn spin_wait(&self, backoff: &mut u32) {
        for _ in 0..*backoff {
            unsafe { core::arch::x86_64::_mm_pause() };
        }
        *backoff = (*backoff * 2).min(64); // Exponential backoff, cap at 64
    }
}