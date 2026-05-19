// C11 threads implementation for w64devkit
// This is free and unencumbered software released into the public domain.
#include <threads.h>
#include <time.h>

typedef __UINTPTR_TYPE__ uz;
typedef __INT32_TYPE__   i32;
typedef __INT64_TYPE__   i64;
typedef __UINT64_TYPE__  u64;

#define W32 [[gnu::dllimport, gnu::stdcall]]
W32 void AcquireSRWLockExclusive(uz *);
W32 i32  CloseHandle(uz);
W32 uz   CreateThread(uz, uz, uz, uz, i32, uz);
W32 void ExitThread[[noreturn]](i32);
W32 i32  FlsAlloc(uz);
W32 i32  FlsFree(i32);
W32 uz   FlsGetValue(i32);
W32 i32  FlsSetValue(i32, uz);
W32 i32  GetCurrentThreadId();
W32 i32  GetExitCodeThread(uz, i32 *);
W32 void GetSystemTimeAsFileTime(u64 *);
W32 i32  InitOnceExecuteOnce(uz *, uz, uz, uz *);
W32 void ReleaseSRWLockExclusive(uz *);
W32 void Sleep(i32);
W32 i32  SleepConditionVariableSRW(uz *, uz *, i32, i32);
W32 i32  SwitchToThread();
W32 i32  TryAcquireSRWLockExclusive(uz *);
W32 i32  WaitForSingleObject(uz, i32);
W32 void WakeAllConditionVariable(uz *);
W32 void WakeConditionVariable(uz *);

typedef struct {
    thrd_start_t fn;
    void        *arg;
    uz           srw;
    uz           cnd;
    i32          consumed;
} Spawn;

[[gnu::stdcall]]
static i32 thread_trampoline(void *p)
{
    Spawn *sp = p;
    thrd_start_t fn = sp->fn;       // fn/arg were written before CreateThread,
    void *arg = sp->arg;            // so they're visible without the lock.
    AcquireSRWLockExclusive(&sp->srw);
    sp->consumed = 1;
    WakeConditionVariable(&sp->cnd);
    ReleaseSRWLockExclusive(&sp->srw);
    return fn(arg);
}

int thrd_create(thrd_t *thr, thrd_start_t fn, void *arg)
{
    Spawn sp = {.fn = fn, .arg = arg};
    i32 tid = 0;
    uz h = CreateThread(0, 0, (uz)&thread_trampoline, (uz)&sp, 0, (uz)&tid);
    if (!h) return thrd_error;
    AcquireSRWLockExclusive(&sp.srw);
    while (!sp.consumed) {
        SleepConditionVariableSRW(&sp.cnd, &sp.srw, -1, 0);
    }
    ReleaseSRWLockExclusive(&sp.srw);

    thr->__handle = (i32)h;
    thr->__tid    = tid;
    return thrd_success;
}

int thrd_equal(thrd_t a, thrd_t b)
{
    return a.__tid == b.__tid;
}

thrd_t thrd_current()
{
    // The returned thrd_t can be passed to thrd_equal but not to
    // thrd_join or thrd_detach (which trap on a null handle). These
    // semantics match MSVC.
    return (thrd_t){0, GetCurrentThreadId()};
}

int thrd_sleep(const struct timespec *d, struct timespec *r)
{
    // Convert to milliseconds with overflow detection. If the duration
    // doesn't fit in i32 (or the math overflows), degrade to an infinite
    // sleep — no real program will run long enough to notice the difference.
    i64 ms;
    i32 timeout = -1;
    if (!__builtin_mul_overflow((i64)d->tv_sec, (i64)1000, &ms) &&
        !__builtin_add_overflow(ms, (i64)((d->tv_nsec + 999999)/1000000), &ms)) {
        if (ms < 0)           ms = 0;
        if (ms <= 0x7fffffff) timeout = (i32)ms;
    }
    Sleep(timeout);
    if (r) {
        r->tv_sec  = 0;
        r->tv_nsec = 0;
    }
    return 0;
}

void thrd_yield()
{
    SwitchToThread();
}

void thrd_exit(int res)
{
    ExitThread(res);
}

int thrd_detach(thrd_t t)
{
    if (!t.__handle) __builtin_trap();
    return CloseHandle(t.__handle) ? thrd_success : thrd_error;
}

int thrd_join(thrd_t t, int *res)
{
    if (!t.__handle) __builtin_trap();
    if (WaitForSingleObject(t.__handle, -1)) return thrd_error;
    if (res) {
        i32 ec = 0;
        if (!GetExitCodeThread(t.__handle, &ec)) {
            CloseHandle(t.__handle);
            return thrd_error;
        }
        *res = ec;
    }
    return CloseHandle(t.__handle) ? thrd_success : thrd_error;
}

int mtx_init(mtx_t *m, int type)
{
    if (type & (mtx_recursive | mtx_timed)) return thrd_error;
    m->__ = 0;
    return thrd_success;
}

int mtx_lock(mtx_t *m)
{
    AcquireSRWLockExclusive(&m->__);
    return thrd_success;
}

int mtx_timedlock(mtx_t *, const struct timespec *)
{
    __builtin_trap();
}

int mtx_trylock(mtx_t *m)
{
    return TryAcquireSRWLockExclusive(&m->__) ? thrd_success : thrd_busy;
}

int mtx_unlock(mtx_t *m)
{
    ReleaseSRWLockExclusive(&m->__);
    return thrd_success;
}

void mtx_destroy(mtx_t *)
{
}

[[gnu::stdcall]]
static i32 once_trampoline(uz, uz parameter, uz *)
{
    void (*func)(void) = (void (*)(void))parameter;
    func();
    return 1;
}

void call_once(once_flag *flag, void (*func)(void))
{
    InitOnceExecuteOnce(&flag->__, (uz)&once_trampoline, (uz)func, 0);
}

int cnd_init(cnd_t *c)
{
    c->__ = 0;
    return thrd_success;
}

int cnd_signal(cnd_t *c)
{
    WakeConditionVariable(&c->__);
    return thrd_success;
}

int cnd_broadcast(cnd_t *c)
{
    WakeAllConditionVariable(&c->__);
    return thrd_success;
}

int cnd_wait(cnd_t *c, mtx_t *m)
{
    if (!SleepConditionVariableSRW(&c->__, &m->__, -1, 0)) {
        return thrd_error;
    }
    return thrd_success;
}

int cnd_timedwait(cnd_t *c, mtx_t *m, const struct timespec *abs)
{
    // FILETIME is 100ns ticks since 1601-01-01 UTC.
    // Unix epoch (1970-01-01) is 11644473600 seconds later:
    //   116444736000000000 ticks.
    // If the conversion overflows (caller asked for an absurdly-far-future
    // deadline) or the relative wait exceeds Win32's max finite timeout,
    // degrade gracefully to an untimed wait.
    u64 now;
    GetSystemTimeAsFileTime(&now);
    i64 abs_ft;
    i32 timeout = -1;
    if (!__builtin_mul_overflow((i64)abs->tv_sec, (i64)10000000, &abs_ft) &&
        !__builtin_add_overflow(abs_ft, (i64)(abs->tv_nsec/100), &abs_ft) &&
        !__builtin_add_overflow(abs_ft, (i64)116444736000000000, &abs_ft)) {
        i64 diff = abs_ft - (i64)now;
        i64 ms   = diff <= 0 ? 0 : diff/10000;
        if (ms <= 0x7fffffff) timeout = (i32)ms;
    }
    if (!SleepConditionVariableSRW(&c->__, &m->__, timeout, 0)) {
        return thrd_timedout;
    }
    return thrd_success;
}

void cnd_destroy(cnd_t *)
{
}

int tss_create(tss_t *key, tss_dtor_t dtor)
{
    i32 k = FlsAlloc((uz)dtor);
    if (k == -1) return thrd_error;
    key->__ = k;
    return thrd_success;
}

void *tss_get(tss_t key)
{
    return (void *)FlsGetValue(key.__);
}

int tss_set(tss_t key, void *val)
{
    return FlsSetValue(key.__, (uz)val) ? thrd_success : thrd_error;
}

void tss_delete(tss_t key)
{
    FlsFree(key.__);
}
