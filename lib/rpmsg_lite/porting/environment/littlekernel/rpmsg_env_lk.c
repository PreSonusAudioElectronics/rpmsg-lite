/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * Copyright (c) 2015 Xilinx, Inc.
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************
 * FILE NAME
 *
 *       rpmsg_env_zephyr.c
 *
 *
 * DESCRIPTION
 *
 *       This file is Zephyr RTOS Implementation of env layer for OpenAMP.
 *
 *
 **************************************************************************/

#include "rpmsg_env.h"
#include "rpmsg_platform.h"
#include "virtqueue.h"
#include "rpmsg_compiler.h"
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/thread.h>
#include <lib/cbuf.h>
#include <stdlib.h>
#include <string.h>
#include <arch/arch_ops.h>
#include <dev/interrupt.h>

#define APP_MU_IRQ_PRIORITY (3U)
#define ENV_NULL ((void*)0)

/*
    Needed:
        semaphore
        mutex
        scheduler lock/unlock
        msg queue
        is_in_isr() ?
*/


#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

/* RL_ENV_MAX_MUTEX_COUNT is an arbitrary count greater than 'count'
   if the inital count is 1, this function behaves as a mutex
   if it is greater than 1, it acts as a "resource allocator" with
   the maximum of 'count' resources available.
   Currently, only the first use-case is applicable/applied in RPMsg-Lite.
 */
#define RL_ENV_MAX_MUTEX_COUNT (10)


extern int32_t MU_IRQ_HANDLER(void);

static int32_t env_init_counter = 0;

// static struct k_sem env_sema    = {0};
static semaphore_t env_sema;

static mutex_t env_mutex;

/* Max supported ISR counts */
#define ISR_COUNT (32U)
/*!
 * Structure to keep track of registered ISR's.
 */
struct isr_info
{
    void *data;
};
static struct isr_info isr_table[ISR_COUNT];

typedef struct env_msg_queue
{
    cbuf_t* cbuf;
    uint16_t msg_size;
} env_msg_queue_t;


static inline void env_enter_critical(void)
{
    rp_platform_global_isr_disable();
    env_mb();
}

static inline void env_exit_critical(void)
{
    rp_platform_global_isr_enable();
    env_mb();
}

/*!
 * env_in_isr
 *
 * @returns - true, if currently in ISR
 *
 */
static int32_t env_in_isr(void)
{
    return rp_platform_in_isr();
}


/*!
 * env_init
 *
 * Initializes OS/BM environment.
 *
 */
int32_t env_init(void *shmem_addr)
{
    int32_t retval;
    // k_sched_lock(); /* stop scheduler */
    THREAD_LOCK(state);
    /* verify 'env_init_counter' */
    RL_ASSERT(env_init_counter >= 0);
    if (env_init_counter < 0)
    {
        // k_sched_unlock(); /* re-enable scheduler */
        THREAD_UNLOCK(state);
        return -1;
    }
    env_init_counter++;
    /* multiple call of 'env_init' - return ok */
    if (env_init_counter == 1)
    {
        /* first call */
        mutex_init(&env_mutex);
        // k_sem_init(&env_sema, 0, 1);
        sem_init(&env_sema, 0);
        (void)memset(isr_table, 0, sizeof(isr_table));
        // k_sched_unlock();
        THREAD_UNLOCK(state);

        retval = rp_platform_init(shmem_addr);
        /* Here Zephyr overrides whatever rp_platform_init() did with 
        interrupt priorities, etc
        */

        // Directly populate the M7 core vector table with the handler address, same as the freertos port
        // IRQ_DIRECT_CONNECT(MU_M7_IRQn, APP_MU_IRQ_PRIORITY, zephMuHandler, 0);
        // irq_enable(MU_IRQS);
        int status = configure_interrupt( MU_A53_IRQn, IRQ_TRIGGER_MODE_EDGE, IRQ_POLARITY_ACTIVE_HIGH);
        RL_ASSERT( status == 0);
        // status = register_int_handler(MU_IRQS, )
        status = unmask_interrupt(MU_A53_IRQn);

        // k_sem_give(&env_sema);
        sem_post(&env_sema, true);

        return retval;
    }
    else
    {
        // k_sched_unlock();
        THREAD_UNLOCK(state);
        /* Get the semaphore and then return it,
         * this allows for rp_platform_init() to block
         * if needed and other tasks to wait for the
         * blocking to be done.
         * This is in ENV layer as this is ENV specific.*/
        // k_sem_take(&env_sema, K_FOREVER);
        sem_wait(&env_sema);
        // k_sem_give(&env_sema);
        sem_post(&env_sema, true);
        return 0;
    }
}

/*!
 * env_deinit
 *
 * Uninitializes OS/BM environment.
 *
 * @returns - execution status
 */
int32_t env_deinit(void)
{
    int32_t retval;

    // k_sched_lock(); /* stop scheduler */
    THREAD_LOCK(state);
    /* verify 'env_init_counter' */
    RL_ASSERT(env_init_counter > 0);
    if (env_init_counter <= 0)
    {
        // k_sched_unlock(); /* re-enable scheduler */
        THREAD_UNLOCK(state);
        return -1;
    }

    /* counter on zero - call platform deinit */
    env_init_counter--;
    /* multiple call of 'env_deinit' - return ok */
    if (env_init_counter <= 0)
    {
        /* last call */
        (void)memset(isr_table, 0, sizeof(isr_table));
        retval = rp_platform_deinit();
        // k_sem_reset(&env_sema);
        sem_destroy(&env_sema);
        // k_sched_unlock();
        THREAD_UNLOCK(state);

        return retval;
    }
    else
    {
        // k_sched_unlock();
        THREAD_UNLOCK(state);
        return 0;
    }
}

/*!
 * env_allocate_memory - implementation
 *
 * @param size
 */
void *env_allocate_memory(uint32_t size)
{
    return (malloc(size));
}

/*!
 * env_free_memory - implementation
 *
 * @param ptr
 */
void env_free_memory(void *ptr)
{
    if (ptr != ((void *)0))
    {
        free(ptr);
    }
}

/*!
 *
 * env_memset - implementation
 *
 * @param ptr
 * @param value
 * @param size
 */
void env_memset(void *ptr, int32_t value, uint32_t size)
{
    (void)memset(ptr, value, size);
}

/*!
 *
 * env_memcpy - implementation
 *
 * @param dst
 * @param src
 * @param len
 */
void env_memcpy(void *dst, void const *src, uint32_t len)
{
    (void)memcpy(dst, src, len);
}

/*!
 *
 * env_strcmp - implementation
 *
 * @param dst
 * @param src
 */

int32_t env_strcmp(const char *dst, const char *src)
{
    return (strcmp(dst, src));
}

/*!
 *
 * env_strncpy - implementation
 *
 * @param dst
 * @param src
 * @param len
 */
void env_strncpy(char *dst, const char *src, uint32_t len)
{
    (void)strncpy(dst, src, len);
}

/*!
 *
 * env_strncmp - implementation
 *
 * @param dst
 * @param src
 * @param len
 */
int32_t env_strncmp(char *dst, const char *src, uint32_t len)
{
    return (strncmp(dst, src, len));
}

/*!
 *
 * env_mb - implementation
 *
 */
void env_mb(void)
{
    smp_mb();
}

/*!
 * env_rmb - implementation
 */
void env_rmb(void)
{
    smp_rmb();
}

/*!
 * env_wmb - implementation
 */
void env_wmb(void)
{
    smp_wmb();
}

/*!
 * env_map_vatopa - implementation
 *
 * @param address
 */
uintptr_t env_map_vatopa(void *address)
{
    return rp_platform_vatopa(address);
}

/*!
 * env_map_patova - implementation
 *
 * @param address
 */
void *env_map_patova(uintptr_t address)
{
    return rp_platform_patova(address);
}

/*!
 * env_create_mutex
 *
 * Creates a mutex with the given initial count.
 *
 */
int32_t env_create_mutex(void **lock, int32_t count)
{
    // struct k_sem *semaphore_ptr;

    // semaphore_ptr = (struct k_sem *)env_allocate_memory(sizeof(struct k_sem));
    // if (semaphore_ptr == ((void *)0))
    // {
    //     return -1;
    // }

    // if (count > RL_ENV_MAX_MUTEX_COUNT)
    // {
    //     return -1;
    // }

    // k_sem_init(semaphore_ptr, count, RL_ENV_MAX_MUTEX_COUNT);
    // *lock = (void *)semaphore_ptr;
    // return 0;
    if( lock == ENV_NULL )
    {
        return -1;
    }
    semaphore_t * sem = env_allocate_memory(sizeof(semaphore_t));
    if( sem == ((void*)0) )
    {
        return -1;
    }

    if ( count > RL_ENV_MAX_MUTEX_COUNT )
    {
        return -1;
    }

    sem_init(sem, count);
    *lock = (void*)sem;
    return 0;
}

/*!
 * env_delete_mutex
 *
 * Deletes the given lock
 *
 */
void env_delete_mutex(void *lock)
{
    // k_sem_reset(lock);
    sem_destroy( (semaphore_t*)lock );
    env_free_memory(lock);
}

/*!
 * env_lock_mutex
 *
 * Tries to acquire the lock, if lock is not available then call to
 * this function will suspend.
 */
void env_lock_mutex(void *lock)
{
    if (env_in_isr() == 0)
    {
        // k_sem_take((struct k_sem *)lock, K_FOREVER);
        sem_wait( (semaphore_t*)lock );
    }
}

/*!
 * env_unlock_mutex
 *
 * Releases the given lock.
 */
void env_unlock_mutex(void *lock)
{
    if (env_in_isr() == 0)
    {
        // k_sem_give((struct k_sem *)lock);
        sem_post( (semaphore_t*)lock, true );
    }
}

/*!
 * env_create_sync_lock
 *
 * Creates a synchronization lock primitive. It is used
 * when signal has to be sent from the interrupt context to main
 * thread context.
 */
int32_t env_create_sync_lock(void **lock, int32_t state)
{
    return env_create_mutex(lock, state); /* state=1 .. initially free */
}

/*!
 * env_delete_sync_lock
 *
 * Deletes the given lock
 *
 */
void env_delete_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_delete_mutex(lock);
    }
}

/*!
 * env_acquire_sync_lock
 *
 * Tries to acquire the lock, if lock is not available then call to
 * this function waits for lock to become available.
 */
void env_acquire_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_lock_mutex(lock);
    }
}

/*!
 * env_release_sync_lock
 *
 * Releases the given lock.
 */
void env_release_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_unlock_mutex(lock);
    }
}

/*!
 * env_sleep_msec
 *
 * Suspends the calling thread for given time , in msecs.
 */
void env_sleep_msec(uint32_t num_msec)
{
    thread_sleep(num_msec);
}

void env_yield(void)
{
    thread_yield();
}

/*!
 * env_register_isr
 *
 * Registers interrupt handler data for the given interrupt vector.
 *
 * @param vector_id - virtual interrupt vector number
 * @param data      - interrupt handler data (virtqueue)
 */
void env_register_isr(uint16_t vector_id, void *data)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        isr_table[vector_id].data = data;
    }
}

/*!
 * env_unregister_isr
 *
 * Unregisters interrupt handler data for the given interrupt vector.
 *
 * @param vector_id - virtual interrupt vector number
 */
void env_unregister_isr(uint16_t vector_id)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        isr_table[vector_id].data = ((void *)0);
    }
}

/*!
 * env_enable_interrupt
 *
 * Enables the given interrupt
 *
 * @param vector_id   - interrupt vector number
 */

void env_enable_interrupt(uint32_t vector_id)
{
    (void)rp_platform_interrupt_enable(vector_id);
}

/*!
 * env_disable_interrupt
 *
 * Disables the given interrupt
 *
 * @param vector_id   - interrupt vector number
 */

void env_disable_interrupt(uint32_t vector_id)
{
    (void)rp_platform_interrupt_disable(vector_id);
}

/*!
 * env_map_memory
 *
 * Enables memory mapping for given memory region.
 *
 * @param pa   - physical address of memory
 * @param va   - logical address of memory
 * @param size - memory size
 * param flags - flags for cache/uncached  and access type
 */

void env_map_memory(uint32_t pa, uint32_t va, uint32_t size, uint32_t flags)
{
    rp_platform_map_mem_region(va, pa, size, flags);
}

/*!
 * env_disable_cache
 *
 * Disables system caches.
 *
 */

void env_disable_cache(void)
{
    rp_platform_cache_all_flush_invalidate();
    rp_platform_cache_disable();
}

/*========================================================= */
/* Util data / functions  */

void env_isr(uint32_t vector)
{
    struct isr_info *info;
    RL_ASSERT(vector < ISR_COUNT);
    if (vector < ISR_COUNT)
    {
        info = &isr_table[vector];
        virtqueue_notification((struct virtqueue *)info->data);
    }
}

/*
 * env_create_queue
 *
 * Creates a message queue.
 *
 * @param queue -  pointer to created queue
 * @param length -  maximum number of elements in the queue
 * @param element_size - queue element size in bytes
 *
 * @return - status of function execution
 */
int32_t env_create_queue(void **queue, int32_t length, int32_t element_size)
{
    mutex_acquire(&env_mutex);
    cbuf_t * cbuf = ENV_NULL;
    char* buf = ENV_NULL;
    env_msg_queue_t* mq;

    if( *queue == ENV_NULL )
    {
        goto cleanup;
    }

    cbuf = env_allocate_memory(sizeof(cbuf));
    if( cbuf == ENV_NULL )
    {
        goto cleanup;
    }

    size_t len = element_size * length;
    buf = env_allocate_memory(len);
    if( buf == ENV_NULL )
    {
        goto cleanup;
    }

    cbuf_initialize_etc(cbuf, len, buf);

    mq = env_allocate_memory(sizeof(env_msg_queue_t));
    if( mq == ENV_NULL )
    {
        goto cleanup;
    }

    mq->cbuf = cbuf;
    mq->msg_size = element_size;

    *queue = mq;
    mutex_release(&env_mutex);
    return 0;

cleanup:
    if( cbuf ) { env_free_memory(cbuf); }
    if( buf ) { env_free_memory(buf); }
    mutex_release(&env_mutex);
    return -1;
}

/*!
 * env_delete_queue
 *
 * Deletes the message queue.
 *
 * @param queue - queue to delete
 */

void env_delete_queue(void *queue)
{
    if( queue != ENV_NULL )
    {
        env_msg_queue_t* mq = (env_msg_queue_t*)queue;
        cbuf_reset( mq->cbuf );
        env_free_memory( mq->cbuf->buf );
        env_free_memory( mq->cbuf );
        env_free_memory( mq );
    }
}

/*!
 * env_put_queue
 *
 * Put an element in a queue.
 *
 * @param queue - queue to put element in
 * @param msg - pointer to the message to be put into the queue
 * @param timeout_ms - timeout in ms
 *
 * @return - status of function execution
 */

int32_t env_put_queue(void *queue, void *msg, uint32_t timeout_ms)
{
    bool re_sched_allowed;
    if (env_in_isr() != 0)
    {
        re_sched_allowed = false;
    }
    else
    {
        re_sched_allowed = timeout_ms > 0 ? true : false;
    }

    env_msg_queue_t* mq = (env_msg_queue_t*)queue;
    size_t written = 0;
    
    cbuf_write(mq->cbuf, msg, mq->msg_size, re_sched_allowed);
    if( written != mq->msg_size )
    {
        return 1;
    }

    return 0;
}

/*!
 * env_get_queue
 *
 * Get an element out of a queue.
 *
 * @param queue - queue to get element from
 * @param msg - pointer to a memory to save the message
 * @param timeout_ms - timeout in ms
 *
 * @return - status of function execution
 */

int32_t env_get_queue(void *queue, void *msg, uint32_t timeout_ms)
{
    bool block;
    if (env_in_isr() != 0)
    {
        block = false;
    }
    else
    {
        block = timeout_ms > 0 ? true : false;
    }

    env_msg_queue_t* mq = (env_msg_queue_t*)queue;
    if( 0 == cbuf_read(mq->cbuf, msg, mq->msg_size, block))
    {
        return 1;
    }

    return 0;
}

/*!
 * env_get_current_queue_size
 *
 * Get current queue size.
 *
 * @param queue - queue pointer
 *
 * @return - Number of queued items in the queue
 */

int32_t env_get_current_queue_size(void *queue)
{
    env_msg_queue_t* mq = (env_msg_queue_t*)queue;
    return cbuf_space_used(mq->cbuf);
}
