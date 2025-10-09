use core::sync::atomic::{AtomicU32, AtomicBool, Ordering};

#[repr(C)]
pub struct RwLock {
    readers: AtomicU32,
    writer: AtomicBool,
    owner: AtomicU32,
    recursion: AtomicU32,
}

impl RwLock {
    pub const fn new() -> Self {
        Self {
            readers: AtomicU32::new(0),
            writer: AtomicBool::new(false),
            owner: AtomicU32::new(0),
            recursion: AtomicU32::new(0),
        }
    }

    #[inline]
    pub fn read_lock(&self, owner_id: u32) {
        if self.writer.load(Ordering::Acquire) && self.owner.load(Ordering::Acquire) == owner_id {
            // The current process holds the write lock, so it can "read"
            return;
        }

        loop {
            while self.writer.load(Ordering::Acquire) {
                unsafe { core::arch::x86_64::_mm_pause() };
            }
            
            self.readers.fetch_add(1, Ordering::AcqRel);
            
            if !self.writer.load(Ordering::Acquire) {
                break;
            }
            
            self.readers.fetch_sub(1, Ordering::AcqRel);
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

        while self.writer.swap(true, Ordering::AcqRel) {
            while self.writer.load(Ordering::Acquire) {
                unsafe { core::arch::x86_64::_mm_pause() };
            }
        }

        while self.readers.load(Ordering::Acquire) > 0 {
            unsafe { core::arch::x86_64::_mm_pause() };
        }

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
}