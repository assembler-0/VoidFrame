#pragma once

#ifdef __cplusplus

class Spinlock {
public:
    Spinlock();
    void lock();
    void unlock();
    bool try_lock();

private:
    volatile int locked;
};

class SpinlockGuard {
public:
    explicit SpinlockGuard(Spinlock& lock);
    ~SpinlockGuard();
    SpinlockGuard(const SpinlockGuard&) = delete;
    SpinlockGuard& operator=(const SpinlockGuard&) = delete;
    SpinlockGuard(SpinlockGuard&&) = delete;
    SpinlockGuard& operator=(SpinlockGuard&&) = delete;
private:
    Spinlock& lock;
};

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

typedef Spinlock Spinlock;

void spinlock_lock(Spinlock* lock);
void spinlock_unlock(Spinlock* lock);
bool spinlock_try_lock(Spinlock* lock);

#ifdef __cplusplus
}
#endif
