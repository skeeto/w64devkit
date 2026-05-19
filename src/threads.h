// C11 threads — w64devkit toolchain implementation
// This is free and unencumbered software released into the public domain.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*thrd_start_t)(void *);

enum {
    thrd_success  = 0,
    thrd_busy     = 1,
    thrd_error    = 2,
    thrd_nomem    = 3,
    thrd_timedout = 4,
};

enum {
    mtx_plain     = 0,
    mtx_recursive = 1,
    mtx_timed     = 2,
};

struct timespec;

typedef struct {
    __INT32_TYPE__ __handle;
    __INT32_TYPE__ __tid;
} thrd_t;
int    thrd_create(thrd_t *, thrd_start_t, void *);
int    thrd_equal(thrd_t, thrd_t);
thrd_t thrd_current(void);
int    thrd_sleep(const struct timespec *, struct timespec *);
void   thrd_yield(void);
void   thrd_exit[[noreturn]](int);
int    thrd_detach(thrd_t);
int    thrd_join(thrd_t, int *);

typedef struct { __UINTPTR_TYPE__ __; } mtx_t;
int    mtx_init(mtx_t *, int);
int    mtx_lock(mtx_t *);
int    mtx_timedlock(mtx_t *, const struct timespec *);
int    mtx_trylock(mtx_t *);
int    mtx_unlock(mtx_t *);
void   mtx_destroy(mtx_t *);

typedef struct { __UINTPTR_TYPE__ __; } once_flag;
#define ONCE_FLAG_INIT {0}
void   call_once(once_flag *, void (*)(void));

typedef struct { __UINTPTR_TYPE__ __; } cnd_t;
int    cnd_init(cnd_t *);
int    cnd_signal(cnd_t *);
int    cnd_broadcast(cnd_t *);
int    cnd_wait(cnd_t *, mtx_t *);
int    cnd_timedwait(cnd_t *, mtx_t *, const struct timespec *);
void   cnd_destroy(cnd_t *);

#if !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#  define thread_local _Thread_local
#endif

enum { TSS_DTOR_ITERATIONS = 1 };

typedef struct { __INT32_TYPE__ __; } tss_t;
typedef void (*tss_dtor_t)(void *);

int    tss_create(tss_t *, tss_dtor_t);
void  *tss_get(tss_t);
int    tss_set(tss_t, void *);
void   tss_delete(tss_t);

#ifdef __cplusplus
}
#endif
