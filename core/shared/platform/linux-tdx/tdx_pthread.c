/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef TDX_DISABLE_PTHREAD

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "tdx_pthread.h"
#include <time.h>

/* TDX guest-host interface calls declarations */
extern int tdcall_pthread_create(void *thread, const void *attr, void *start_routine, void *arg);
extern int tdcall_pthread_join(void *thread, void **retval);
extern int tdcall_pthread_detach(void *thread);
extern int tdcall_pthread_equal(void *t1, void *t2);
extern void *tdcall_pthread_self(void);
extern int tdcall_pthread_cancel(void *thread);
extern int tdcall_pthread_setcancelstate(int state, int *oldstate);
extern int tdcall_pthread_setcanceltype(int type, int *oldtype);
extern void tdcall_pthread_testcancel(void);
extern int tdcall_pthread_mutex_init(void *mutex, void *attr);
extern int tdcall_pthread_mutex_destroy(void *mutex);
extern int tdcall_pthread_mutex_lock(void *mutex);
extern int tdcall_pthread_mutex_trylock(void *mutex);
extern int tdcall_pthread_mutex_unlock(void *mutex);
extern int tdcall_pthread_cond_init(void *cond, void *attr);
extern int tdcall_pthread_cond_destroy(void *cond);
extern int tdcall_pthread_cond_wait(void *cond, void *mutex);
extern int tdcall_pthread_cond_timedwait(void *cond, void *mutex, void *abstime);
extern int tdcall_pthread_cond_signal(void *cond);
extern int tdcall_pthread_cond_broadcast(void *cond);
extern int tdcall_pthread_rwlock_init(void *rwlock, void *attr);
extern int tdcall_pthread_rwlock_destroy(void *rwlock);
extern int tdcall_pthread_rwlock_rdlock(void *rwlock);
extern int tdcall_pthread_rwlock_tryrdlock(void *rwlock);
extern int tdcall_pthread_rwlock_wrlock(void *rwlock);
extern int tdcall_pthread_rwlock_trywrlock(void *rwlock);
extern int tdcall_pthread_rwlock_unlock(void *rwlock);
extern int tdcall_pthread_key_create(void *key, void *destructor);
extern int tdcall_pthread_key_delete(unsigned int key);
extern int tdcall_pthread_setspecific(unsigned int key, const void *value);
extern void *tdcall_pthread_getspecific(unsigned int key);

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
               void *(*start_routine)(void *), void *arg)
{
    return tdcall_pthread_create(thread, attr, start_routine, arg);
}

int
pthread_join(pthread_t thread, void **retval)
{
    return tdcall_pthread_join(&thread, retval);
}

int
pthread_detach(pthread_t thread)
{
    return tdcall_pthread_detach(&thread);
}

int
pthread_equal(pthread_t t1, pthread_t t2)
{
    return tdcall_pthread_equal(&t1, &t2);
}

pthread_t
pthread_self(void)
{
    return (pthread_t)tdcall_pthread_self();
}

int
pthread_cancel(pthread_t thread)
{
    return tdcall_pthread_cancel(&thread);
}

int
pthread_setcancelstate(int state, int *oldstate)
{
    return tdcall_pthread_setcancelstate(state, oldstate);
}

int
pthread_setcanceltype(int type, int *oldtype)
{
    return tdcall_pthread_setcanceltype(type, oldtype);
}

void
pthread_testcancel(void)
{
    tdcall_pthread_testcancel();
}

/* Mutex functions */
int
pthread_mutex_init(pthread_mutex_t *mutex, const void *attr)
{
    return tdcall_pthread_mutex_init(mutex, (void *)attr);
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return tdcall_pthread_mutex_destroy(mutex);
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return tdcall_pthread_mutex_lock(mutex);
}

int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return tdcall_pthread_mutex_trylock(mutex);
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return tdcall_pthread_mutex_unlock(mutex);
}

/* Condition variable functions */
int
pthread_cond_init(pthread_cond_t *cond, const void *attr)
{
    return tdcall_pthread_cond_init(cond, (void *)attr);
}

int
pthread_cond_destroy(pthread_cond_t *cond)
{
    return tdcall_pthread_cond_destroy(cond);
}

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return tdcall_pthread_cond_wait(cond, mutex);
}

int
pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                       const struct timespec *abstime)
{
    return tdcall_pthread_cond_timedwait(cond, mutex, (void *)abstime);
}

int
pthread_cond_signal(pthread_cond_t *cond)
{
    return tdcall_pthread_cond_signal(cond);
}

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
    return tdcall_pthread_cond_broadcast(cond);
}

/* Read-write lock functions */
int
pthread_rwlock_init(pthread_rwlock_t *rwlock, const void *attr)
{
    return tdcall_pthread_rwlock_init(rwlock, (void *)attr);
}

int
pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_destroy(rwlock);
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_rdlock(rwlock);
}

int
pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_tryrdlock(rwlock);
}

int
pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_wrlock(rwlock);
}

int
pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_trywrlock(rwlock);
}

int
pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    return tdcall_pthread_rwlock_unlock(rwlock);
}

/* Thread-specific data functions */
int
pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    return tdcall_pthread_key_create(key, destructor);
}

int
pthread_key_delete(pthread_key_t key)
{
    return tdcall_pthread_key_delete(key);
}

int
pthread_setspecific(pthread_key_t key, const void *value)
{
    return tdcall_pthread_setspecific(key, value);
}

void *
pthread_getspecific(pthread_key_t key)
{
    return tdcall_pthread_getspecific(key);
}

#endif /* TDX_DISABLE_PTHREAD */