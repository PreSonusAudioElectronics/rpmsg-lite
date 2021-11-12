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
#include <zephyr.h>
#include "rpmsg_platform.h"
#include "virtqueue.h"
#include "rpmsg_compiler.h"
#include "rpmsg_config.h"
#include "rpmsg_lite.h"
#include "rpmsg_trace.h"

#define APP_MU_IRQ_PRIORITY (3U)

#define LOCAL_TRACE (1)

#include <stdlib.h>
#include <string.h>

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 0)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 1"
#endif

/* RL_ENV_MAX_MUTEX_COUNT is an arbitrary count greater than 'count'
   if the inital count is 1, this function behaves as a mutex
   if it is greater than 1, it acts as a "resource allocator" with
   the maximum of 'count' resources available.
   Currently, only the first use-case is applicable/applied in RPMsg-Lite.
 */
#define RL_ENV_MAX_MUTEX_COUNT (10)

extern int32_t MU_IRQ_HANDLER(void);

static struct k_sem env_sema    = {0};

/* Max supported ISR's per MU channel */
#define ENV_N_ISRS_PER_CHAN (32U)
/*!
 * Structure to keep track of registered ISR's.
 */
struct isr_info
{
    void *data;
};
static struct isr_info isr_table[RL_N_PLATFORM_CHANS][ENV_N_ISRS_PER_CHAN] = {0};

struct env_context
{
    uint32_t phy_channel;
    uintptr_t shmem_address;
    bool initialized;
};

static struct env_context environments[RL_N_PLATFORM_CHANS] = {0};

static inline struct env_context *get_context(void *env)
{
    RL_ASSERT( env );
    struct env_context *context = (struct env_context*)env;
    return context;
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


ISR_DIRECT_DECLARE(zephMuHandler)
{
    MU_IRQ_HANDLER();
    ISR_DIRECT_PM();
    return 1;
}

/*!
 * env_init
 *
 * Initializes OS/BM environment.
 *
 */
int32_t env_init(void **env_context, void *env_init_data)
{
    int32_t retval = RL_SUCCESS;

    if( !env_context || !env_init_data ) {
        retval = RL_ERR_PARAM;
        goto exit;
    }

    struct rpmsg_lite_env_init_data *init_data = (struct rpmsg_lite_env_init_data*)env_init_data;

    uint32_t phy_channel = init_data->mu_channel;

    if( environments[phy_channel].initialized ) {
        retval = RL_ERR_PARAM;
        goto exit;
    }

    if( init_data->shmem_addr == NULL ) {
        retval = RL_ERR_PARAM;
        goto exit;
    }

    if( init_data->mu_channel >= RL_N_PLATFORM_CHANS ) {
        retval = RL_ERR_PARAM;
        goto exit;
    }
    
    k_sched_lock();

    environments[phy_channel].phy_channel = phy_channel;
    environments[phy_channel].shmem_address = (uintptr_t)init_data->shmem_addr;

    /* first call */
    k_sem_init(&env_sema, 0, 1);

    retval = rp_platform_init( init_data->shmem_addr, phy_channel );

    // Directly populate the M7 core vector table with the handler address, same as the freertos port
    IRQ_DIRECT_CONNECT(MU_M7_IRQn, APP_MU_IRQ_PRIORITY, zephMuHandler, 0);

    environments[phy_channel].initialized = true;
    *env_context = &( environments[phy_channel] );
    k_sem_give(&env_sema);

    k_sched_unlock();
    irq_enable(MU_M7_IRQn);
exit:
    return retval;
}


int32_t env_deinit(void *env)
{
    struct env_context *context = get_context(env);

    RL_ASSERT_MSG("Can't de-init uninitialized environment!\n",
        context->initialized );

    k_sched_lock(); /* stop scheduler */

    /* last call */
    (void)memset(isr_table[context->phy_channel], 0, ENV_N_ISRS_PER_CHAN * sizeof(struct isr_info));
    k_sem_reset(&env_sema);
    k_sched_unlock();

    return 0;
}

// not used in this port
void env_register_isr(void *env, uint32_t vector_id, void *data)
{
    RLTRACE_ENTRY;
}
// not used in this port
void env_unregister_isr(void * env, uint32_t vector_id)
{
    RLTRACE_ENTRY;
}

// using asserts since returns are not checked by caller
int32_t env_init_interrupt(void *env, int32_t vq_id, void *isr_data)
{
    struct env_context *context = get_context(env);
    RL_ASSERT( context->initialized );
    if( isr_table[context->phy_channel][vq_id].data != NULL ) {
        RLTRACEF("WARNING: overwriting existing isr setting.\n");
    }
    RL_ASSERT(vq_id < ENV_N_ISRS_PER_CHAN);
    if (vq_id < ENV_N_ISRS_PER_CHAN)
    {
        isr_table[context->phy_channel][vq_id].data = isr_data;
    }
    return 0;
}

int32_t env_deinit_interrupt(void *env, int32_t vq_id)
{
    struct env_context *context = get_context(env);
    RL_ASSERT(vq_id < ENV_N_ISRS_PER_CHAN);
    if( isr_table[context->phy_channel][vq_id].data == NULL ) {
        RLTRACEF("WARNING: already unregistered!\n");
    }

    isr_table[context->phy_channel][vq_id].data = NULL;
    return 0;
}


void env_isr(void *env, uint32_t vector)
{
    struct env_context *context = get_context(env);
    uint32_t channel = context->phy_channel;
    RL_ASSERT(vector < ENV_N_ISRS_PER_CHAN);
    if (vector < ENV_N_ISRS_PER_CHAN)
    {
        struct isr_info *info = &isr_table[channel][vector];
        virtqueue_notification((struct virtqueue *)info->data);
    }
}

int env_get_channel( void *env )
{
    struct env_context *context = get_context(env);
    if( context != &(environments[context->phy_channel]) ) {
        return -1;
    }
    else {
        return context->phy_channel;
    }
}

void *env_get_env(uint32_t channel)
{
    RL_ASSERT( channel < RL_N_PLATFORM_CHANS );
    RL_ASSERT( environments[channel].initialized );
    return &(environments[channel]);
}

bool env_is_initialized( void *shmem_base )
{
    for( unsigned i=0; i<RL_N_PLATFORM_CHANS; ++i )
    {
        if( environments[i].shmem_address == (uintptr_t)shmem_base )
        {
            return environments[i].initialized;
        }
    }
    return false;
}

void env_enable_interrupt(void *env, uint32_t vector_id)
{
    struct env_context *context = get_context(env);
    (void)rp_platform_interrupt_enable( context->phy_channel );
}


void env_disable_interrupt(void * env, uint32_t vector_id)
{
    struct env_context *context = get_context(env);
    (void)rp_platform_interrupt_disable( context->phy_channel );
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

void env_map_memory(uintptr_t pa, uintptr_t *va, uint32_t size, uint32_t flags)
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
    struct k_msgq *queue_ptr = ((void *)0);
    char *msgq_buffer_ptr    = ((void *)0);

    queue_ptr       = (struct k_msgq *)env_allocate_memory(sizeof(struct k_msgq));
    msgq_buffer_ptr = (char *)env_allocate_memory(length * element_size);
    if ((queue_ptr == ((void *)0)) || (msgq_buffer_ptr == ((void *)0)))
    {
        return -1;
    }
    k_msgq_init(queue_ptr, msgq_buffer_ptr, element_size, length);

    *queue = (void *)queue_ptr;
    return 0;
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
    k_msgq_purge((struct k_msgq *)queue);
    env_free_memory(((struct k_msgq *)queue)->buffer_start);
    env_free_memory(queue);
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
    if (env_in_isr() != 0)
    {
        timeout_ms = 0; /* force timeout == 0 when in ISR */
    }

    if (0 == k_msgq_put((struct k_msgq *)queue, msg, K_MSEC(timeout_ms)))
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
    if (env_in_isr() != 0)
    {
        timeout_ms = 0; /* force timeout == 0 when in ISR */
    }

    if (0 == k_msgq_get((struct k_msgq *)queue, msg, K_MSEC(timeout_ms)))
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
    return k_msgq_num_used_get((struct k_msgq *)queue);
}

/*!
 * env_allocate_memory - implementation
 *
 * @param size
 */
void *env_allocate_memory(uint32_t size)
{
    return (k_malloc(size));
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
        k_free(ptr);
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

int env_strnlen(const char *str, uint32_t maxLen)
{
    unsigned i = 0;
    if( NULL == str )
    {
        return -1;
    }
    while( i < maxLen )
    {
        if( str[i] == '\0' )
        {
            return (int)i;
        }
        i++;
    }
    // we hit the max length
    return -1;
}

/*!
 *
 * env_mb - implementation
 *
 */
void env_mb(void)
{
    MEM_BARRIER();
}

/*!
 * env_rmb - implementation
 */
void env_rmb(void)
{
    MEM_BARRIER();
}

/*!
 * env_wmb - implementation
 */
void env_wmb(void)
{
    MEM_BARRIER();
}

/*!
 * env_map_vatopa - implementation
 *
 * @param address
 */
uintptr_t env_map_vatopa(void *env, void *address)
{
    return rp_platform_vatopa(address);
}

/*!
 * env_map_patova - implementation
 *
 * @param address
 */
void *env_map_patova(void *env, uintptr_t address)
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
    struct k_sem *semaphore_ptr;

    semaphore_ptr = (struct k_sem *)env_allocate_memory(sizeof(struct k_sem));
    if (semaphore_ptr == ((void *)0))
    {
        return -1;
    }

    if (count > RL_ENV_MAX_MUTEX_COUNT)
    {
        return -1;
    }

    k_sem_init(semaphore_ptr, count, RL_ENV_MAX_MUTEX_COUNT);
    *lock = (void *)semaphore_ptr;
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
    k_sem_reset(lock);
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
        k_sem_take((struct k_sem *)lock, K_FOREVER);
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
        k_sem_give((struct k_sem *)lock);
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
    k_sleep(K_MSEC(num_msec));
}

void env_yield(void)
{
    k_yield();
}

void *env_get_platform_context(void *env_context)
{
    // We've just stored the env context pointer so return it directly
    return env_context;
}

inline void env_cache_sync_range(uintptr_t addr, size_t len)
{}

inline void env_cache_invalidate_range(uintptr_t addr, size_t len)
{}
