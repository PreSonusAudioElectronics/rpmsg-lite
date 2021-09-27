/*!
 * \file rpmsg_env_linux_sim.c
 * \author Dave Anderson (danderson@presonus.com)
 * \brief 
 * 
 * Copyright (c)2021 PreSonus Audio Electronics
 * 
 */

#include "rpmsg_env.h"
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
// static struct k_sem env_sema	= {0};

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

/*!
 * env_in_isr
 *
 * @returns - true, if currently in ISR
 *
 */
static int32_t env_in_isr(void)
{
	return platform_in_isr();
}


// ISR_DIRECT_DECLARE(zephMuHandler)
// {
//	 MU_IRQ_HANDLER();
//	 ISR_DIRECT_PM();
//	 return 1;
// }

/*!
 * env_init
 *
 * Initializes OS/BM environment.
 *
 */
int32_t env_init(void *shmem_addr)
{
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

		retval = platform_init(shmem_addr);
		/* Here Zephyr overrides whatever platform_init() did with 
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
		 * this allows for platform_init() to block
		 * if needed and other tasks to wait for the
		 * blocking to be done.
		 * This is in ENV layer as this is ENV specific.*/
		// k_sem_take(&env_sema, K_FOREVER);
		// k_sem_give(&env_sema);
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

	std::mutex mutex;

	mutex.lock(); /* stop scheduler */

	/* verify 'env_init_counter' */
	RL_ASSERT(env_init_counter > 0);
	if (env_init_counter <= 0)
	{
		mutex.unlock(); /* re-enable scheduler */
		return -1;
	}

	/* counter on zero - call platform deinit */
	env_init_counter--;
	/* multiple call of 'env_deinit' - return ok */
	if (env_init_counter <= 0)
	{
		/* last call */
		(void)memset(isr_table, 0, sizeof(isr_table));
		retval = platform_deinit();
		// k_sem_reset(&env_sema);
		mutex.unlock();

		return retval;
	}
	else
	{
		mutex.unlock();
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
uint32_t env_map_vatopa(void *address)
{
	return platform_vatopa(address);
}

/*!
 * env_map_patova - implementation
 *
 * @param address
 */
void *env_map_patova(uint32_t address)
{
	return platform_patova(address);
}

/*!
 * env_create_mutex
 *
 * Creates a mutex with the given initial count.
 *
 */
int32_t env_create_mutex(void **lock, int32_t count)
{
	std::mutex * mutex = new std::mutex;

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
	if(lock)
	{
		std::mutex *mutex = static_cast<std::mutex*>( lock );
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
		if(lock)
		{
			std::mutex *mutex = static_cast<std::mutex*>( lock );
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
		std::mutex *mutex = static_cast<std::mutex*>( lock );
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
 * @param vector_id - virtual interrupt vector number
 * @param data	  - interrupt handler data (virtqueue)
 */
void env_register_isr(uint32_t vector_id, void *data)
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
void env_unregister_isr(uint32_t vector_id)
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
	(void)platform_interrupt_enable(vector_id);
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
	(void)platform_interrupt_disable(vector_id);
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
	platform_map_mem_region(va, pa, size, flags);
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
	if(queue)
	{
		delete( (ThreadsafeQueue*)queue );
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
	if (0 == ((ThreadsafeQueue*)queue)->put(msg) )
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
	if ( 0 == ((ThreadsafeQueue*)queue)->get(msg) )
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
