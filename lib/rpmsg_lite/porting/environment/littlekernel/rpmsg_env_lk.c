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


#include "rpmsg_lite.h"
#include "rpmsg_env.h"
#include "rpmsg_platform.h"
#include "virtqueue.h"
#include "rpmsg_compiler.h"
#include "rpmsg_trace.h"

#include <lib/cbuf.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <arch/arch_ops.h>


#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/thread.h>
#include <dev/interrupt.h>
#include <kernel/vm.h>
#include <delay.h>

// #define APP_MU_IRQ_PRIORITY (3U)
#define ENV_NULL ((void*)0)

#define LOCAL_TRACE (0)


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

static uintptr_t gSharedMemBaseAddr = 0;

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

inline void env_flush_spin(void)
{
    udelay(50000);
}

static int32_t env_in_isr(void)
{
    return rp_platform_in_isr();
}

int32_t env_init(void **shmem_addr)
{
    RLTRACE_ENTRY;
    if( NULL == shmem_addr ) {
        return RL_ERR_PARAM;
    }

    int32_t retval;
    /* stop scheduler */
    THREAD_LOCK(state);
    /* verify 'env_init_counter' */
    RL_ASSERT(env_init_counter >= 0);
    if (env_init_counter < 0)
    {
        /* re-enable scheduler */
        THREAD_UNLOCK(state);
        RLTRACE_EXIT;
        return -1;
    }
    env_init_counter++;
    /* multiple call of 'env_init' - return ok */
    if (env_init_counter == 1)
    {
        /* first call */
        mutex_init(&env_mutex);
        sem_init(&env_sema, 0);
        (void)memset(isr_table, 0, sizeof(isr_table));
        THREAD_UNLOCK(state);

        /*!
         * Adjust shmem_addr to account for offset between Jailhouse guest
         * "physical" address and hardware physical address
         */
        paddr_t targetPa = (paddr_t)*shmem_addr + RL_ENV_VIRTSTART_OFFSET_FROM_PHY;

        RLTRACEF("Attempt allocating to specific phys addr %p..\n", (void*)targetPa);
        void *sharedMem = NULL;
        int status = vmm_alloc_physical(vmm_get_kernel_aspace(), "rpmsg shared mem",
            RPMSG_LITE_SHAREDMEM_SIZE, &sharedMem, 12,
            targetPa, 0, ARCH_MMU_FLAG_UNCACHED);
        
        if( NO_ERROR != status )
        {
            RLTRACEF("Failed to allocate shared mem!\n");
            RL_ASSERT(0);
        }

        RLTRACEF("vmm_alloc_physical returned NO_ERROR\n");
        RLTRACEF("Attempt to derefernce the first byte of shared mem..\n");
        RLTRACEF("New va = %p\n", sharedMem);
        paddr_t sharedMemPa = vaddr_to_paddr(sharedMem);
        RLTRACEF("New pa = %p\n", (void*)sharedMemPa);

        char *temp = (char*)sharedMem;
        if( NULL == temp )
        {
            RL_ASSERT(0);
        }

        char tempChar = *temp;

        RLTRACEF("The first byte at start of shared mem is 0x%x\n", tempChar );

        // Overrite pointer with new virtual address
        *shmem_addr = sharedMem;
        retval = rp_platform_init(shmem_addr);
        gSharedMemBaseAddr = (uintptr_t)( *shmem_addr );
        sem_post(&env_sema, true);
        RLTRACE_EXIT;
        return retval;
    }
    else
    {
        THREAD_UNLOCK(state);
        /* Get the semaphore and then return it,
         * this allows for rp_platform_init() to block
         * if needed and other tasks to wait for the
         * blocking to be done.
         * This is in ENV layer as this is ENV specific.*/
        sem_wait(&env_sema);
        sem_post(&env_sema, true);
        RLTRACE_EXIT;
        return 0;
    }
}

int32_t env_deinit(void)
{
    RLTRACE_ENTRY;
    int32_t retval;

    // k_sched_lock(); /* stop scheduler */
    THREAD_LOCK(state);
    /* verify 'env_init_counter' */
    RL_ASSERT(env_init_counter > 0);
    if (env_init_counter <= 0)
    {
        // k_sched_unlock(); /* re-enable scheduler */
        THREAD_UNLOCK(state);
        RLTRACE_EXIT;
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

        RLTRACE_EXIT;
        return retval;
    }
    else
    {
        // k_sched_unlock();
        THREAD_UNLOCK(state);
        RLTRACE_EXIT;
        return 0;
    }
}

void *env_allocate_memory(uint32_t size)
{
    void *retval = malloc(size);
    return retval;
}

void env_free_memory(void *ptr)
{
    RLTRACEF("enter with ptr: %p\n", ptr);
    if (ptr != ((void *)0))
    {
        free(ptr);
    }
    RLTRACE_EXIT;
}

void env_memset(void *ptr, int32_t value, uint32_t size)
{
    void *ret = memset(ptr, value, size);
    if( ret != ptr )
    {
        RLTRACEF("Fail!  memset returned: %p\n", ret );
    }
}

void env_memcpy(void *dst, void const *src, uint32_t len)
{
    (void)memcpy(dst, src, len);
}

int32_t env_strcmp(const char *dst, const char *src)
{
    return (strcmp(dst, src));
}

void env_strncpy(char *dst, const char *src, uint32_t len)
{
    (void)strncpy(dst, src, len);
}

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

void env_mb(void)
{
    smp_mb();
}

void env_rmb(void)
{
    smp_rmb();
}

void env_wmb(void)
{
    smp_wmb();
}

uintptr_t env_map_vatopa(void *address)
{
    return rp_platform_vatopa(address);
}

void *env_map_patova(uintptr_t address)
{
    return rp_platform_patova(address);
}

int32_t env_create_mutex(void **lock, int32_t count)
{
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

void env_delete_mutex(void *lock)
{
    sem_destroy( (semaphore_t*)lock );
    env_free_memory(lock);
}

void env_lock_mutex(void *lock)
{
    if (env_in_isr() == 0)
    {
        // k_sem_take((struct k_sem *)lock, K_FOREVER);
        sem_wait( (semaphore_t*)lock );
    }
}

void env_unlock_mutex(void *lock)
{
    if (env_in_isr() == 0)
    {
        // k_sem_give((struct k_sem *)lock);
        sem_post( (semaphore_t*)lock, true );
    }
}

int32_t env_create_sync_lock(void **lock, int32_t state)
{
    return env_create_mutex(lock, state); /* state=1 .. initially free */
}

void env_delete_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_delete_mutex(lock);
    }
}

void env_acquire_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_lock_mutex(lock);
    }
}

void env_release_sync_lock(void *lock)
{
    if (lock != ((void *)0))
    {
        env_unlock_mutex(lock);
    }
}

void env_sleep_msec(uint32_t num_msec)
{
    thread_sleep(num_msec);
}

void env_yield(void)
{
    thread_yield();
}

void env_register_isr(uint16_t vector_id, void *data)
{
    RLTRACE_ENTRY;
    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        isr_table[vector_id].data = data;
    }
    RLTRACE_EXIT;
}

void env_unregister_isr(uint16_t vector_id)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        isr_table[vector_id].data = ((void *)0);
    }
}

void env_enable_interrupt(uint32_t vector_id)
{
    (void)rp_platform_interrupt_enable(vector_id);
}

void env_disable_interrupt(uint32_t vector_id)
{
    RLTRACEF("vector_id: %d\n", vector_id);
    int status = rp_platform_interrupt_disable(vector_id);
    if( vector_id != (uint32_t)status )
    {
        RL_ASSERT_MSG("rp_platform_interrupt_disable failed!\n", 0 );
    }
}

void env_map_memory(uintptr_t pa, uintptr_t *va, uint32_t size, uint32_t flags)
{
    rp_platform_map_mem_region(va, pa, size, flags);
}

void env_disable_cache(void)
{
    rp_platform_cache_all_flush_invalidate();
    rp_platform_cache_disable();
}

/*========================================================= */
/* Util data / functions  */

inline void env_isr(uint32_t vector)
{
    struct isr_info *info;
    RL_ASSERT(vector < ISR_COUNT);
    if (vector < ISR_COUNT)
    {
        info = &isr_table[vector];
        virtqueue_notification((struct virtqueue *)info->data);
    }
}

int32_t env_create_queue(void **queue, int32_t length, int32_t element_size)
{
    mutex_acquire(&env_mutex);
    cbuf_t * cbuf = ENV_NULL;
    char* buf = ENV_NULL;
    env_msg_queue_t* mq;

    if( queue == ENV_NULL )
    {
        goto cleanup;
    }

    cbuf = env_allocate_memory(sizeof(cbuf_t));
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

int32_t env_put_queue(void *queue, void *msg, uint32_t timeout_ms)
{
    RLTRACE_ENTRY;
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
    
    written = cbuf_write(mq->cbuf, msg, mq->msg_size, re_sched_allowed);
    if( written != mq->msg_size )
    {
        RLTRACEF("return 1\n");
        return 1;
    }

    RLTRACEF("return 0\n");
    return 0;
}

int32_t env_get_queue(void *queue, void *msg, uint32_t timeout_ms)
{
    RLTRACE_ENTRY;
    int32_t retval = 0;
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
    retval = cbuf_read(mq->cbuf, msg, mq->msg_size, block);

    RLTRACEF("return %d\n", retval);
    return retval;
}

int32_t env_get_current_queue_size(void *queue)
{
    env_msg_queue_t* mq = (env_msg_queue_t*)queue;
    return cbuf_space_used(mq->cbuf);
}

inline void env_cache_sync_range(uintptr_t addr, size_t len)
{
    rp_platform_sync_cache_range(addr, len);
}

inline void env_cache_invalidate_range(uintptr_t addr, size_t len)
{
    rp_platform_invalidate_cache_range(addr, len);
}
