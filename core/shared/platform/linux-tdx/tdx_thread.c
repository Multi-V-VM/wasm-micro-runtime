/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#ifndef TDX_DISABLE_PTHREAD

typedef struct {
    thread_start_routine_t start_routine;
    void *arg;
} thread_wrapper_arg;

static void *
os_thread_wrapper(void *arg)
{
    thread_wrapper_arg *targ = arg;
    thread_start_routine_t start_func = targ->start_routine;
    void *thread_arg = targ->arg;

#if 0
    os_printf("THREAD CREATED %p\n", &targ);
#endif
    BH_FREE(targ);
    start_func(thread_arg);
    return NULL;
}

int
os_thread_create_with_prio(korp_tid *tid, thread_start_routine_t start,
                           void *arg, unsigned int stack_size, int prio)
{
    pthread_attr_t tattr;
    thread_wrapper_arg *targ;

    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE);
    if (stack_size > 0)
        pthread_attr_setstacksize(&tattr, stack_size);

    targ = (thread_wrapper_arg *)BH_MALLOC(sizeof(*targ));
    if (!targ) {
        return BHT_ERROR;
    }

    targ->start_routine = start;
    targ->arg = arg;

    if (pthread_create(tid, &tattr, os_thread_wrapper, targ) != 0) {
        BH_FREE(targ);
        return BHT_ERROR;
    }

    pthread_attr_destroy(&tattr);
    return BHT_OK;
}

int
os_thread_create(korp_tid *tid, thread_start_routine_t start, void *arg,
                 unsigned int stack_size)
{
    return os_thread_create_with_prio(tid, start, arg, stack_size,
                                      BH_THREAD_DEFAULT_PRIORITY);
}

korp_tid
os_self_thread()
{
    return pthread_self();
}

int
os_thread_join(korp_tid thread, void **value_ptr)
{
    return pthread_join(thread, value_ptr);
}

int
os_thread_detach(korp_tid thread)
{
    return pthread_detach(thread);
}

void
os_thread_exit(void *retval)
{
    return pthread_exit(retval);
}

int
os_mutex_init(korp_mutex *mutex)
{
    return pthread_mutex_init(mutex, NULL);
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    return pthread_mutex_destroy(mutex);
}

int
os_mutex_lock(korp_mutex *mutex)
{
    return pthread_mutex_lock(mutex);
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    return pthread_mutex_unlock(mutex);
}

int
os_cond_init(korp_cond *cond)
{
    return pthread_cond_init(cond, NULL);
}

int
os_cond_destroy(korp_cond *cond)
{
    return pthread_cond_destroy(cond);
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    return pthread_cond_wait(cond, mutex);
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
    struct timespec ts;
    uint64 now;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return BHT_ERROR;
    }

    now = ((uint64)ts.tv_sec) * 1000000 + ((uint64)ts.tv_nsec) / 1000;
    useconds = now + useconds;

    ts.tv_sec = useconds / 1000000;
    ts.tv_nsec = (useconds % 1000000) * 1000;

    if (pthread_cond_timedwait(cond, mutex, &ts) != 0) {
        return BHT_ERROR;
    }

    return BHT_OK;
}

int
os_cond_signal(korp_cond *cond)
{
    return pthread_cond_signal(cond);
}

int
os_cond_broadcast(korp_cond *cond)
{
    return pthread_cond_broadcast(cond);
}

int
os_thread_signal_init()
{
    /* Signal handling initialization for TDX environment */
    /* This is a simplified implementation */
    return BHT_OK;
}

void
os_thread_signal_destroy()
{
    /* Signal handling cleanup for TDX environment */
}

bool
os_thread_signal_inited()
{
    /* In TDX, we assume signals are always initialized */
    return true;
}

#if defined(OS_ENABLE_HW_BOUND_CHECK) && defined(BH_PLATFORM_WINDOWS)
os_signal_handler
os_thread_signal_set(int sig, os_signal_handler handler)
{
    /* Signal handler setting for TDX environment */
    /* This is a simplified implementation */
    return NULL;
}
#endif

int
os_sem_init(korp_sem *sem, unsigned int init_count)
{
    /* Semaphore implementation using mutex and condition variable */
    /* This would need a proper implementation */
    return BHT_OK;
}

int
os_sem_destroy(korp_sem *sem)
{
    return BHT_OK;
}

int
os_sem_wait(korp_sem *sem)
{
    return BHT_OK;
}

int
os_sem_reltimed_wait(korp_sem *sem, uint64 useconds)
{
    return BHT_OK;
}

int
os_sem_signal(korp_sem *sem)
{
    return BHT_OK;
}

int
os_rwlock_init(korp_rwlock *rwlock)
{
    return pthread_rwlock_init(rwlock, NULL);
}

int
os_rwlock_rdlock(korp_rwlock *rwlock)
{
    return pthread_rwlock_rdlock(rwlock);
}

int
os_rwlock_wrlock(korp_rwlock *rwlock)
{
    return pthread_rwlock_wrlock(rwlock);
}

int
os_rwlock_unlock(korp_rwlock *rwlock)
{
    return pthread_rwlock_unlock(rwlock);
}

int
os_rwlock_destroy(korp_rwlock *rwlock)
{
    return pthread_rwlock_destroy(rwlock);
}

#endif /* end of TDX_DISABLE_PTHREAD */