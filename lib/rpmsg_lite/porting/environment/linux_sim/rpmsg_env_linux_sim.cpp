/*!
 * \file rpmsg_env_linux_sim.c
 * \author Dave Anderson (danderson@presonus.com)
 * \brief 
 * 
 * Copyright (c)2021 PreSonus Audio Electronics
 * 
 */

#include "rpmsg_env.h"
#include "rpmsg_env_linux.h"
#include "rpmsg_platform.h"
#include "virtqueue.h"
#include "rpmsg_compiler.h"
#include "threadsafequeue.h"

#define APP_MU_IRQ_PRIORITY (3U)

#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <algorithm>

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT != 1)
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

static int32_t env_init_counter = 0;
// static struct k_sem env_sema	= {0};

/* Max supported ISR counts */
#define ISR_COUNT (32U)
/*!
 * Structure to keep track of registered ISR's.
 */

struct isr_info
{
    virtqueue * vq;
    bool registered = false;
};

typedef struct env_context
{
    int addIsr(virtqueue *vq, unsigned vector_id)
    {
        if(vector_id >= ISR_COUNT )
        {
            return -1;
        }

        if ( isr_table[vector_id].registered )
        {
            return -1;
        }

        isr_table[vector_id].vq = vq;
        isr_table[vector_id].registered = true;
        if( highest_registered_vector_id < vector_id )
        {
            highest_registered_vector_id = vector_id;
        }
        return 0;
    }

    void *rp_platform_context;
    void *shared_mem_base;
    isr_info isr_table[ISR_COUNT];
    unsigned highest_registered_vector_id = 0;
} env_context_t;

static std::mutex envLock;
static std::vector<env_context_t *> registeredEnvironments;

/**
 * Returns pointer to platform context.
 *
 * @param env_context Pointer to env. context
 *
 * @return Pointer to platform context
 */
void *env_get_rp_platform_context(void *env_context)
{
    // env_context_t *env = static_cast<env_context_t *>(env_context);
    // return env->rp_platform_context;

    // use the env context
    return env_context;
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
int32_t env_init(void **env_context, void *env_init_data)
{
    std::lock_guard<std::mutex> lk(envLock);

    rpmsg_env_init_t *init = static_cast<rpmsg_env_init_t *>(env_init_data);

    RL_ASSERT(init != nullptr);

    env_context_t *ctx = static_cast<env_context_t *>(env_allocate_memory(sizeof(env_context_t)));

    RL_ASSERT(ctx != nullptr);
    RL_ASSERT(rp_platform_init(init->shmemAddr) >= 0);

    ctx->shared_mem_base = init->shmemAddr;

    *env_context = ctx;
    registeredEnvironments.push_back(ctx);

    return 0;

#if 0
    int32_t retval;
    std::mutex mutex;
    mutex.lock(); /* stop scheduler */
    /* verify 'env_init_counter' */
    RL_ASSERT(env_init_counter >= 0);
    if (env_init_counter < 0)
    {
        mutex.unlock(); /* re-enable scheduler */
        return -1;
    }
    env_init_counter++;
    /* multiple call of 'env_init' - return ok */
    if (env_init_counter == 1)
    {
        /* first call */
        // k_sem_init(&env_sema, 0, 1);
        (void)memset(isr_table, 0, sizeof(isr_table));
        mutex.unlock();

        retval = rp_platform_init(shmem_addr);
        /* Here Zephyr overrides whatever rp_platform_init() did with 
        interrupt priorities, etc
        */

        // Directly populate the M7 core vector table with the handler address, same as the freertos port
        // IRQ_DIRECT_CONNECT(MU_M7_IRQn, APP_MU_IRQ_PRIORITY, zephMuHandler, 0);
        // irq_enable(MU_M7_IRQn);

        // k_sem_give(&env_sema);

        return retval;
    }
    else
    {
        mutex.unlock();
        /* Get the semaphore and then return it,
         * this allows for rp_platform_init() to block
         * if needed and other tasks to wait for the
         * blocking to be done.
         * This is in ENV layer as this is ENV specific.*/
        // k_sem_take(&env_sema, K_FOREVER);
        // k_sem_give(&env_sema);
        return 0;
    }
#endif
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
    env_context_t *ctx = static_cast<env_context_t *>(env_context);
    RL_ASSERT( env_context != nullptr );
    rp_platform_deinit(ctx->rp_platform_context);

    // clear the env memory
    *ctx = {0};
    
    // delete this environment from the list of registered environments
    auto result = std::find(registeredEnvironments.begin(), 
        registeredEnvironments.end(), ctx );
    if( result != registeredEnvironments.end() )
    {
        auto idx = std::distance(registeredEnvironments.begin(), result );
        registeredEnvironments.erase(registeredEnvironments.begin() + idx);
    }
    env_free_memory(ctx);
    return 0;
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
    return reinterpret_cast<uintptr_t>(address);

#if 0
#if IMX_MMAP_VA_ON_PA
    return ((uint32_t)address);
#else
    /* This is faster then mem_offset64() */
    env_context_t *ctx = env;
    uint64_t va        = (uint64_t)address;
    uint64_t va_start  = (uint64_t)ctx->va;
    uint64_t pa        = ctx->pa + (va - va_start);
    return pa;
#endif
#endif
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
    std::mutex *mutex = new std::mutex;

    if (count > RL_ENV_MAX_MUTEX_COUNT)
    {
        return -1;
    }

    *lock = (void *)mutex;
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
    if (lock)
    {
        std::mutex *mutex = static_cast<std::mutex *>(lock);
        delete mutex;
    }
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
        if (lock)
        {
            std::mutex *mutex = static_cast<std::mutex *>(lock);
            mutex->lock();
        }
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
        std::mutex *mutex = static_cast<std::mutex *>(lock);
        mutex->unlock();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(num_msec));
}

void env_yield(void)
{
    std::this_thread::yield();
}

/*!
 * env_register_isr
 *
 * Registers interrupt handler data for the given interrupt vector.
 *
 * @param env - environment context
 * @param vector_id - virtual interrupt vector number
 * @param data	  - interrupt handler data (virtqueue)
 */
void env_register_isr(void *env, uint32_t vector_id, void *data)
{
    RL_ASSERT(vector_id < ISR_COUNT);
    RL_ASSERT(env != nullptr);

    virtqueue *vq = static_cast<virtqueue*>( data );

    RL_ASSERT( vq != nullptr );

    env_context_t *thisEnv = static_cast<env_context_t *>(env);

    int status = thisEnv->addIsr(vq, vector_id);

    RL_ASSERT( status >= 0 );
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
    RL_ASSERT(env != nullptr);
    // TODO: provide unregister
}

/*!
 * env_enable_interrupt
 *
 * Enables the given interrupt
 *
 * @param vector_id   - virtual interrupt vector number
 */
void env_enable_interrupt(void *env, uint32_t vector_id)
{
    env_context_t *ctx = static_cast<env_context_t *>(env);

    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        // TODO: do we need to enable/disable interrupts in simulation?
        // ctx->isr_table[vector_id].enabled = 1;
    }
}

/*!
 * env_disable_interrupt
 *
 * Disables the given interrupt
 *
 * @param vector_id   - virtual interrupt vector number
 */
void env_disable_interrupt(void *env, uint32_t vector_id)
{
    env_context_t *ctx = static_cast<env_context_t *>(env);

    RL_ASSERT(vector_id < ISR_COUNT);
    if (vector_id < ISR_COUNT)
    {
        // TODO: do we need to disable/enable interrupts in simulation?
        // ctx->isr_table[vector_id].enabled = 0;
    }
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

void env_isr(void *env, uint32_t vector)
{
    RL_ASSERT(env != nullptr);

    env_context_t * pEnv = static_cast<env_context_t*>( env );

    /*
        On a real SoC, this is where we would kick the other
        processor core through the Messaging Unit

        We simulate that here by notifying all virtqueues
        (from all registered environments) except this one,
        which have registered an isr for this vector
    */

    for ( auto it:registeredEnvironments )
    {
        if( it != pEnv )
        {
            // notify this virtqueue
            if( it->isr_table[vector].registered )
            {
                virtqueue_notification( it->isr_table[vector].vq );
            }
        }
    }
}

/**
 * Called by rpmsg to init an interrupt
 *
 * @param env      Pointer to env context.
 * @param vq_id    Virt. queue ID.
 * @param isr_data Pointer to interrupt data.
 *
 * @return         Execution status.
 */
int32_t env_init_interrupt(void *env, int32_t vq_id, void *isr_data)
{
    env_register_isr(env, vq_id, isr_data);
    return 0;
}

/**
 * Called by rpmsg to deinit an interrupt.
 *
 * @param env   Pointer to env context.
 * @param vq_id Virt. queue ID.
 *
 * @return      Execution status.
 */
int32_t env_deinit_interrupt(void *env, int32_t vq_id)
{
    env_unregister_isr(env, vq_id);
    return 0;
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
    auto p1 = new ThreadsafeQueue(length, element_size);
    *queue = p1;
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
    if (queue)
    {
        delete ((ThreadsafeQueue *)queue);
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
    if (env_in_isr() != 0)
    {
        timeout_ms = 0; /* force timeout == 0 when in ISR */
    }

    // ignore timeout
    if (0 == ((ThreadsafeQueue *)queue)->put(msg))
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

    // ignore timeout
    if (0 == ((ThreadsafeQueue *)queue)->get(msg))
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
    return ((ThreadsafeQueue *)queue)->getSize();
}
