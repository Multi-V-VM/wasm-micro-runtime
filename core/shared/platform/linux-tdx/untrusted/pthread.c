/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* TDX Host-side pthread operations implementation */

int
tdcall_pthread_create(void *thread, const void *attr, void *start_routine, void *arg)
{
    return pthread_create((pthread_t *)thread, (const pthread_attr_t *)attr,
                          (void *(*)(void *))start_routine, arg);
}

int
tdcall_pthread_join(void *thread, void **retval)
{
    return pthread_join(*(pthread_t *)thread, retval);
}

int
tdcall_pthread_detach(void *thread)
{
    return pthread_detach(*(pthread_t *)thread);
}

int
tdcall_pthread_equal(void *t1, void *t2)
{
    return pthread_equal(*(pthread_t *)t1, *(pthread_t *)t2);
}

void *
tdcall_pthread_self(void)
{
    static pthread_t self;
    self = pthread_self();
    return &self;
}

int
tdcall_pthread_cancel(void *thread)
{
    return pthread_cancel(*(pthread_t *)thread);
}

int
tdcall_pthread_setcancelstate(int state, int *oldstate)
{
    return pthread_setcancelstate(state, oldstate);
}

int
tdcall_pthread_setcanceltype(int type, int *oldtype)
{
    return pthread_setcanceltype(type, oldtype);
}

void
tdcall_pthread_testcancel(void)
{
    pthread_testcancel();
}

int
tdcall_pthread_mutex_init(void *mutex, void *attr)
{
    return pthread_mutex_init((pthread_mutex_t *)mutex, 
                              (const pthread_mutexattr_t *)attr);
}

int
tdcall_pthread_mutex_destroy(void *mutex)
{
    return pthread_mutex_destroy((pthread_mutex_t *)mutex);
}

int
tdcall_pthread_mutex_lock(void *mutex)
{
    return pthread_mutex_lock((pthread_mutex_t *)mutex);
}

int
tdcall_pthread_mutex_trylock(void *mutex)
{
    return pthread_mutex_trylock((pthread_mutex_t *)mutex);
}

int
tdcall_pthread_mutex_unlock(void *mutex)
{
    return pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

int
tdcall_pthread_cond_init(void *cond, void *attr)
{
    return pthread_cond_init((pthread_cond_t *)cond,
                             (const pthread_condattr_t *)attr);
}

int
tdcall_pthread_cond_destroy(void *cond)
{
    return pthread_cond_destroy((pthread_cond_t *)cond);
}

int
tdcall_pthread_cond_wait(void *cond, void *mutex)
{
    return pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
}

int
tdcall_pthread_cond_timedwait(void *cond, void *mutex, void *abstime)
{
    return pthread_cond_timedwait((pthread_cond_t *)cond, 
                                  (pthread_mutex_t *)mutex,
                                  (const struct timespec *)abstime);
}

int
tdcall_pthread_cond_signal(void *cond)
{
    return pthread_cond_signal((pthread_cond_t *)cond);
}

int
tdcall_pthread_cond_broadcast(void *cond)
{
    return pthread_cond_broadcast((pthread_cond_t *)cond);
}

int
tdcall_pthread_rwlock_init(void *rwlock, void *attr)
{
    return pthread_rwlock_init((pthread_rwlock_t *)rwlock,
                               (const pthread_rwlockattr_t *)attr);
}

int
tdcall_pthread_rwlock_destroy(void *rwlock)
{
    return pthread_rwlock_destroy((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_rwlock_rdlock(void *rwlock)
{
    return pthread_rwlock_rdlock((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_rwlock_tryrdlock(void *rwlock)
{
    return pthread_rwlock_tryrdlock((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_rwlock_wrlock(void *rwlock)
{
    return pthread_rwlock_wrlock((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_rwlock_trywrlock(void *rwlock)
{
    return pthread_rwlock_trywrlock((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_rwlock_unlock(void *rwlock)
{
    return pthread_rwlock_unlock((pthread_rwlock_t *)rwlock);
}

int
tdcall_pthread_key_create(void *key, void *destructor)
{
    return pthread_key_create((pthread_key_t *)key, (void (*)(void *))destructor);
}

int
tdcall_pthread_key_delete(unsigned int key)
{
    return pthread_key_delete((pthread_key_t)key);
}

int
tdcall_pthread_setspecific(unsigned int key, const void *value)
{
    return pthread_setspecific((pthread_key_t)key, value);
}

void *
tdcall_pthread_getspecific(unsigned int key)
{
    return pthread_getspecific((pthread_key_t)key);
}