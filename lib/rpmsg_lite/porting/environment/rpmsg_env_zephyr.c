/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * Copyright (c) 2015 Xilinx, Inc.
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2021 NXP
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
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/drivers/mm/system_mm.h>

#include "rpmsg_platform.h"
#include "virtqueue.h"
#include "rpmsg_compiler.h"
#include "rpmsg_lite.h"

#include <stdlib.h>
#include <string.h>

#if !defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 1"
#endif

/* RL_ENV_MAX_MUTEX_COUNT is an arbitrary count greater than 'count'
   if the inital count is 1, this function behaves as a mutex
   if it is greater than 1, it acts as a "resource allocator" with
   the maximum of 'count' resources available.
   Currently, only the first use-case is applicable/applied in RPMsg-Lite.
 */
#define RL_ENV_MAX_MUTEX_COUNT (10)

#define RL_ENV_MAX_MEM_MAPS 8u


/*!
 * env_in_isr
 *
 * @returns - true, if currently in ISR
 *
 */
static int32_t env_in_isr(void)
{
    return k_is_in_isr ();
}

void env_wait_for_link_up(void* env, volatile uint32_t *link_state, uint32_t link_id)
{
    rl_env_t *rl_env = (rl_env_t*)env;
    RL_ASSERT (rl_env);
    k_event_wait (&(rl_env->tx_events), (1 << link_id), true, K_FOREVER);
}

void env_tx_callback(void *rpmsg_lite_dev)
{
    struct rpmsg_lite_instance *instance = (struct rpmsg_lite_instance*)rpmsg_lite_dev;
    RL_ASSERT (instance);
    rl_env_t *env = (rl_env_t*)(instance->env);
    k_event_post (&(env->tx_events), (1 << instance->link_id));
}

/*!
 * env_init
 *
 * Initializes OS/BM environment.
 * allocates env struct, inits and passes back to caller
 *
 */
int32_t env_init(void **env_context, void *env_init_data)
{
    int32_t retval;
    rl_env_t* env = *(rl_env_t**)env_context;
    bool needToAllocate = false;
    rl_env_cfg_t *env_cfg = (rl_env_cfg_t*)env_init_data;
    k_sched_lock(); /* stop scheduler */
    if (!env)
    {
        needToAllocate = true;
    }
    /* multiple call of 'env_init' - return ok */
    if (needToAllocate)
    {
        /* first call */
        needToAllocate = false;
        k_sched_unlock();

        env = env_allocate_memory(sizeof(rl_env_t));
        if(!env)
        {
            return -1;
        }
        memset (env, 0, sizeof(rl_env_t));

        retval = k_sem_init(&(env->sema), 0, 1);
        if (retval != 0)
        {
            goto free_env;
        }

        (void)memset(env->isr_table, 0, sizeof(env->isr_table));
        
        /* 
            Map shared memory to virtual so we can access it. On non-MMU
            systems this call writes back the physical address
        */
        env_cfg = (rl_env_cfg_t*)env_init_data;
        RL_ASSERT(env_cfg);

        env_map_memory (env_cfg->shmem_pa, &(env->shmem_va), 
            env_cfg->shmem_size, WB_CACHE | RL_MEM_NGNRE | RL_MEM_PERM_RW);
        RL_ASSERT (env->shmem_va);
        env->shmem_size = env_cfg->shmem_size;
        
        // done in platform for now
        // env_map_memory (env_cfg->sgi_mbox_pa, &(env->sgi_mbox_va),
        //     env_cfg->sgi_mbox_size, WB_CACHE | RL_MEM_NGNRE | RL_MEM_PERM_RW);
        // RL_ASSERT(env->sgi_mbox_va);
        // env->sgi_mbox_size = env_cfg->sgi_mbox_size;

        env_map_memory(env_cfg->vring_buf_pa,  &(env->vring_buf_va),
            env_cfg->vring_buf_size, UNCACHED | RL_MEM_PERM_RW);
        RL_ASSERT(env->vring_buf_va);

        env->phy_channel = env_cfg->phy_channel;


        env->key = k_spin_lock(&(env->spinlock));
        k_spin_unlock(&(env->spinlock), env->key);
        
        k_event_init(&(env->tx_events));

        retval = platform_init(env);
        
        env->initialized = true;
        k_sem_give(&(env->sema));

        return retval;
    }
    else
    {
        k_sched_unlock();
        /* Get the semaphore and then return it,
         * this allows for platform_init() to block
         * if needed and other tasks to wait for the
         * blocking to be done.
         * This is in ENV layer as this is ENV specific.*/
        k_sem_take(&(env->sema), K_FOREVER);
        k_sem_give(&(env->sema));
        return 0;
    }

free_env:
    env_free_memory (env);
    return retval;
}

/*!
 * env_deinit
 *
 * Uninitializes OS/BM environment.
 *
 * @returns - execution status
 */
int32_t env_deinit(void *env_context)
{
    rl_env_t *env = (rl_env_t*)env_context;
    RL_ASSERT(env);

    env->key = k_spin_lock(&(env->spinlock));
    k_spin_unlock(&(env->spinlock), env->key);
    (void)memset(env->isr_table, 0, sizeof(env->isr_table));
    int32_t retval = platform_deinit();
    RL_ASSERT (retval == 0);
    k_sem_reset(&(env->sema));
    env_free_memory (env);

    return 0;
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


/**************************************************************************/
/*
    VIRT MEMORY MAPPING
*/


/*!
 * env_map_vatopa - implementation
 *
 * @param address
 */
uintptr_t env_map_vatopa(void *env, void *address)
{
    (void)env;
    uintptr_t ret = 0;

    int status = sys_mm_drv_page_phys_get (address, &ret);
    RL_ASSERT (status == 0);
    return ret;
}

/*!
 * env_map_patova - implementation
 *
 * @param address
 */
void *env_map_patova(void *env, uintptr_t phyAddr)
{
    // rl_env_t *rl_env = (rl_env_t *)env;
    // RL_ASSERT(rl_env);
    // uintptr_t locPhyAddr = (uintptr_t)rl_env->shmem_addr_phy;
    // RL_ASSERT(phyAddr > locPhyAddr &&
    //     phyAddr < (locPhyAddr + rl_env->shmem_size));
    
    // uintptr_t diff = phyAddr - locPhyAddr;
    // uintptr_t ret = (uintptr_t)rl_env->shmem_addr_virt + diff;
    // return (void*)ret;
    
    /* Is a physical address really what is needed here? */
    return (void*)phyAddr;
}



/**************************************************************************/



/*!
 * env_create_mutex
 *
 * Creates a mutex with the given initial count.
 *
 */
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
int32_t env_create_mutex(void **lock, int32_t count, void *context)
#else
int32_t env_create_mutex(void **lock, int32_t count)
#endif
{
    struct k_sem *semaphore_ptr;

    if (count > RL_ENV_MAX_MUTEX_COUNT)
    {
        return -1;
    }

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    semaphore_ptr = (struct k_sem *)context;
#else
    semaphore_ptr = (struct k_sem *)env_allocate_memory(sizeof(struct k_sem));
#endif
    if (semaphore_ptr == ((void *)0))
    {
        return -1;
    }

    k_sem_init(semaphore_ptr, count, RL_ENV_MAX_MUTEX_COUNT);
    /* Becasue k_sem_init() does not return any status, we do not know if all is OK or not.
       If something would not be OK dynamically allocated memory has to be freed here. */

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
#if !(defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1))
    env_free_memory(lock);
#endif
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
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
int32_t env_create_sync_lock(void **lock, int32_t state, void *context)
{
    return env_create_mutex(lock, state, context); /* state=1 .. initially free */
}
#else
int32_t env_create_sync_lock(void **lock, int32_t state)
{
    return env_create_mutex(lock, state); /* state=1 .. initially free */
}
#endif

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

/*!
 * env_register_isr
 *
 * Registers interrupt handler data for the given interrupt vector.
 *
 * @param vector_id - virtual interrupt vector number
 * @param data      - interrupt handler data (virtqueue)
 */
void env_register_isr(void *env, uint32_t vector_id, void *data)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    rl_env_t *l_env = (rl_env_t*)env;
    RL_ASSERT(l_env);
    l_env->key = k_spin_lock (&l_env->spinlock);
    if (vector_id < ISR_COUNT)
    {
        l_env->isr_table[vector_id].data = data;
    }
    k_spin_unlock (&l_env->spinlock, l_env->key);
}

/*!
 * env_unregister_isr
 *
 * Unregisters interrupt handler data for the given interrupt vector.
 *
 * @param vector_id - virtual interrupt vector number
 */
void env_unregister_isr(void *env, uint32_t vector_id)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    rl_env_t *l_env = (rl_env_t*)env;
    RL_ASSERT(l_env);
    l_env->key = k_spin_lock (&l_env->spinlock);
    if (vector_id < ISR_COUNT)
    {
        l_env->isr_table[vector_id].data = ((void *)0);
    }
    k_spin_unlock (&l_env->spinlock, l_env->key);
}

int32_t env_init_interrupt(void *env, int32_t vq_id, void *isr_data)
{
    env_register_isr(env, vq_id, isr_data);
    return 0;
}

int env_register_isr_handler (uint32_t irq_num, rl_int_handler_t handler, void *priv_data)
{
    /* FIXME: hard-coded priority */
    int status = irq_connect_dynamic (irq_num, 2, (void (*)(const void *))handler, priv_data, 0);
    return (status == irq_num) ? 0 : -1;
}


/*!
 * env_enable_interrupt
 *
 * Enables the given interrupt
 *
 * @param vector_id   - interrupt vector number
 */

void env_enable_interrupt(void *env, uint32_t vector_id)
{
    (void)env;
    irq_enable (vector_id);
}

/*!
 * env_disable_interrupt
 *
 * Switchted to a spinlock
 *
 * @param vector_id   - interrupt vector number
 */

void env_disable_interrupt(void *env, uint32_t vector_id)
{
    (void)env;
    irq_disable(vector_id);
}

void env_map_memory(uintptr_t pa, void **va, uint32_t size, uint32_t flags)
{
    uintptr_t temp;
    uint32_t l_flags = 0;

    if (flags & UNCACHED) l_flags |= K_MEM_CACHE_NONE;
    if (flags & WB_CACHE) l_flags |= K_MEM_CACHE_WB;
    if (flags & WT_CACHE) l_flags |= K_MEM_CACHE_WT;
    if (flags & RL_MEM_NGNRE) l_flags |= K_MEM_ARM_DEVICE_nGnRE;
    if (flags & RL_MEM_PERM_RW) l_flags |= K_MEM_PERM_RW;
    device_map (&temp, pa, size, l_flags);
    *va = (void*) temp;
}


/*!
 * env_disable_cache
 *
 * Disables system caches.
 *
 */

void env_disable_cache(void)
{
    platform_cache_all_flush_invalidate();
    platform_cache_disable();
}

/*========================================================= */
/* Util data / functions  */

void env_isr(void *env, uint32_t vector)
{
    rl_isr_info_t *info = NULL;
    rl_env_t *l_env = (rl_env_t*)env;
    RL_ASSERT (l_env);
    RL_ASSERT(vector < ISR_COUNT);
    if (vector < ISR_COUNT)
    {
        info = &l_env->isr_table[vector];
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
 * @param queue_static_storage - pointer to queue static storage buffer
 * @param queue_static_context - pointer to queue static context
 *
 * @return - status of function execution
 */
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
int32_t env_create_queue(void **queue,
                         int32_t length,
                         int32_t element_size,
                         uint8_t *queue_static_storage,
                         rpmsg_static_queue_ctxt *queue_static_context)
#else
int32_t env_create_queue(void **queue, int32_t length, int32_t element_size)
#endif
{
    struct k_msgq *queue_ptr = ((void *)0);
    char *msgq_buffer_ptr    = ((void *)0);

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    queue_ptr       = (struct k_msgq *)queue_static_context;
    msgq_buffer_ptr = (char *)queue_static_storage;
#else
    queue_ptr       = (struct k_msgq *)env_allocate_memory(sizeof(struct k_msgq));
    msgq_buffer_ptr = (char *)env_allocate_memory(length * element_size);
#endif
    if ((queue_ptr == ((void *)0)) || (msgq_buffer_ptr == ((void *)0)))
    {
        return -1;
    }
    k_msgq_init(queue_ptr, msgq_buffer_ptr, element_size, length);
    /* Becasue k_msgq_init() does not return any status, we do not know if all is OK or not.
       If something would not be OK dynamically allocated memory has to be freed here. */

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
#if !(defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1))
    env_free_memory(((struct k_msgq *)queue)->buffer_start);
    env_free_memory(queue);
#endif
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

int32_t env_put_queue(void *queue, void *msg, uintptr_t timeout_ms)
{
    if (env_in_isr() != 0)
    {
        timeout_ms = 0; /* force timeout == 0 when in ISR */
    }

    k_timeout_t localTimeout = 
    {
        .ticks = timeout_ms
    };

    if (0 == k_msgq_put((struct k_msgq *)queue, msg, localTimeout))
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

int32_t env_get_queue(void *queue, void *msg, uintptr_t timeout_ms)
{
    if (env_in_isr() != 0)
    {
        timeout_ms = 0; /* force timeout == 0 when in ISR */
    }

    k_timeout_t locTimeout = 
    {
        .ticks = timeout_ms
    };

    if (0 == k_msgq_get((struct k_msgq *)queue, msg, locTimeout))
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
