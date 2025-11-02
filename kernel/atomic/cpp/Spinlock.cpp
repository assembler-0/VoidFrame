#include <Spinlock.h>

#ifdef __cplusplus

Spinlock::Spinlock() : locked(false) {}

void Spinlock::lock() {
    while (__atomic_test_and_set(&locked, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&locked, __ATOMIC_RELAXED))
            __asm__ __volatile__("pause");
    }
}

void Spinlock::unlock() {
    __atomic_clear(&locked, __ATOMIC_RELEASE);
}

bool Spinlock::try_lock() {
    return !__atomic_test_and_set(&locked, __ATOMIC_ACQUIRE);
}

SpinlockGuard::SpinlockGuard(Spinlock& lock) : lock(lock) {
    lock.lock();
}

SpinlockGuard::~SpinlockGuard() {
    lock.unlock();
}

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

void spinlock_lock(Spinlock* lock) {
    lock->lock();
}

void spinlock_unlock(Spinlock* lock) {
    lock->unlock();
}

bool spinlock_try_lock(Spinlock* lock) {
    return lock->try_lock();
}

#ifdef __cplusplus
}
#endif