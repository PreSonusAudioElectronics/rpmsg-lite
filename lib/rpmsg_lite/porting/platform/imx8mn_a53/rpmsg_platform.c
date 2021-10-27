// std lib
#include <stdio.h>
#include <string.h>

// rpmsg_lite
#include "rpmsg_platform.h"
#include "rpmsg_lite.h"
#include "rpmsg_env.h"
#include "rsc_table.h"

// LK
#include <arch/arch_ops.h>
#include <arch/ops.h>
#include <dev/driver.h>
#include <dev/class/msgunit.h>
#include <sys/types.h>
#include <err.h>
#include <kernel/vm.h>

#include "fsl_device_registers.h"

#include <MIMX8MN6_ca53.h>

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

static void on_rx(uint32_t msg);
static void on_tx(void);

static int32_t isr_counter     = 0;
static int32_t disable_counter = 0;
static void *rp_platform_lock;
static struct device *gDev = NULL;

int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
    RLTRACE_ENTRY;
    /* Register ISR to environment layer */
    env_register_isr(vector_id, isr_data);

    if( isr_counter == 0 )
    {
        int status = class_msgunit_register_tx_callback(gDev, RPMSG_MU_CHANNEL, on_tx);
        if( NO_ERROR != status ) {
            RL_ASSERT(0);
        }

        status = class_msgunit_register_rx_callback(gDev, RPMSG_MU_CHANNEL, on_rx);
        if( NO_ERROR != status ) {
            RL_ASSERT(0);
        }
    }
    
    isr_counter++;
    RLTRACEF("Registered isr num: %d\n", isr_counter );

    env_flush_spin();

    env_unlock_mutex(rp_platform_lock);

    RLTRACE_EXIT;
    return 0;
}

int32_t rp_platform_deinit_interrupt(uint32_t vector_id)
{
    RLTRACE_ENTRY;
    /* Prepare the MU Hardware */
    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 < isr_counter);
    isr_counter--;
    if (isr_counter == 0)
    {
        // TODO: use driver for this
        // MU_DisableInterrupts(gMuRegisters, (1UL << 27U) >> RPMSG_MU_CHANNEL);
    }

    /* Unregister ISR from environment layer */
    env_unregister_isr(vector_id);

    env_unlock_mutex(rp_platform_lock);

    RLTRACE_EXIT;
    return 0;
}

void rp_platform_notify(uint32_t vector_id)
{
    RLTRACE_ENTRY;
    uint32_t msg = (uint32_t)(vector_id << 16);

    msg = 0xaaaa;

    env_lock_mutex(rp_platform_lock);
    // TODO: use driver for this
    // MU_SendMsg(gMuRegisters, RPMSG_MU_CHANNEL, msg);
    if( gDev ) {
        int status = class_msgunit_send_msg(gDev, RPMSG_MU_CHANNEL, msg);
        RL_ASSERT( RL_SUCCESS == status );
    }
    else {
        RLTRACEF("We have no device configured!\n");
    }

    env_unlock_mutex(rp_platform_lock);

    RLTRACE_EXIT;
}

static void on_rx(uint32_t msg)
{
    RLTRACE_ENTRY;
}

static void on_tx(void)
{
    RLTRACE_ENTRY;
}


/**
 * rp_platform_time_delay
 *
 * @param num_msec Delay time in ms.
 *
 * This is not an accurate delay, it ensures at least num_msec passed when return.
 */
void rp_platform_time_delay(uint32_t num_msec)
{
    // not implemented
}

/**
 * rp_platform_in_isr
 *
 * Return whether CPU is processing IRQ
 *
 * @return True for IRQ, false otherwise.
 *
 */
int32_t rp_platform_in_isr(void)
{
    // return (((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0UL) ? 1 : 0);
    // TODO: fixme
    return 0;
}

/**
 * rp_platform_interrupt_enable
 *
 * Enable peripheral-related interrupt
 *
 * @param vector_id Virtual vector ID that needs to be converted to IRQ number
 *
 * @return vector_id Return value is never checked.
 *
 */
int32_t rp_platform_interrupt_enable(uint32_t vector_id)
{
    RL_ASSERT(0 < disable_counter);

    rp_platform_global_isr_disable();
    disable_counter--;

    int status;
    int32_t retval = (int32_t)vector_id;

    if (disable_counter == 0)
    {
        // TODO: use driver api
        // status = unmask_interrupt(MU_IRQ_VECTOR);
        retval = NO_ERROR==status ? (int32_t)vector_id : RL_ERRORS_BASE;
    }
    rp_platform_global_isr_enable();
    return ( retval );
}

/**
 * rp_platform_interrupt_disable
 *
 * Disable peripheral-related interrupt.
 *
 * @param vector_id Virtual vector ID that needs to be converted to IRQ number
 *
 * @return vector_id Return value is never checked.
 *
 */
int32_t rp_platform_interrupt_disable(uint32_t vector_id)
{
    RLTRACE_ENTRY;
    RL_ASSERT(0 <= disable_counter);

    rp_platform_global_isr_disable();
    /* virtqueues use the same NVIC vector
       if counter is set - the interrupts are disabled */
    int32_t status, retval = (int32_t)vector_id;
    if (disable_counter == 0)
    {
        // TODO: use driver api
        // status = mask_interrupt(MU_IRQ_VECTOR);
        retval = NO_ERROR==status ? (int32_t)vector_id : RL_ERRORS_BASE;
    }
    disable_counter++;
    rp_platform_global_isr_enable();
    RLTRACE_EXIT;
    return ( retval );
}

// call this with ARCH_MMU_FLAG_CACHED
/*!
 * @brief Map physical memory to virtual address range
 * 
 * @param vrt_addr will be written by 
 * @param phy_addr 
 * @param size 
 * @param flags 
 */
void rp_platform_map_mem_region(uintptr_t *vrt_addr, uintptr_t phy_addr, uint32_t size, 
    uint32_t flags)
{
    RLTRACEF("rp_platform_map_mem_region() called\n");
    RLTRACEF(" arg1: 0x%lx, arg2: 0x%lx, arg3: %d\n",
    (uintptr_t)vrt_addr, phy_addr, size);
    int ret = vmm_alloc_physical(vmm_get_kernel_aspace(), "rpmsg_lite_shmem",
    size, (void**)vrt_addr, 0, phy_addr,
    VMM_FLAG_VALLOC_SPECIFIC, flags);

    if( ret != 0 )
    {
        RL_PRINTF("Failed to map shared physical memory: %d: %s\n", ret, strerror(ret) );
        RL_ASSERT( 0 );
    }
}

/**
 * rp_platform_cache_all_flush_invalidate
 *
 * Dummy implementation
 *
 */
void rp_platform_cache_all_flush_invalidate(void)
{
}

/**
 * rp_platform_cache_disable
 *
 * Dummy implementation
 *
 */
void rp_platform_cache_disable(void)
{
    // dummy stub
}

/**
 * rp_platform_vatopa
 *
 * Dummy implementation
 *
 */
uintptr_t rp_platform_vatopa(void *addr)
{
    return vaddr_to_paddr( addr );
}

/**
 * rp_platform_patova
 *
 *
 */
void *rp_platform_patova(uintptr_t addr)
{
    return paddr_to_kvaddr( addr );
}

void testFuncPtr(void)
{
    RL_PRINTF("The function pointer test!\n");
}

/**
 * rp_platform_init
 *
 * platform/environment init
 */
int32_t rp_platform_init(void **shmem_addr)
{
    RLTRACE_ENTRY;

    RLTRACEF("Copying resource table...\n");
    // copyResourceTable(ptr);

    RLTRACEF("Initializing Messaging Unit...\n");

    gDev = class_msgunit_get_device_by_id(RL_LK_MU_BUS_ID);
    if( NULL == gDev )
    {
        RLTRACEF("Failed to acquire messaging unit device at bus id %d\n", RL_LK_MU_BUS_ID);
        RLTRACE_EXIT;
        return -1;
    }

    env_flush_spin();

    RLTRACEF("Creating platform mutex...\n");
    /* Create lock used in multi-instanced RPMsg */
    if (0 != env_create_mutex(&rp_platform_lock, 1))
    {
        RLTRACE_EXIT;
        return -1;
    }

    env_flush_spin();

    RLTRACE_EXIT;
    return 0;
}

/**
 * rp_platform_deinit
 *
 * platform/environment deinit process
 */
int32_t rp_platform_deinit(void)
{
    RLTRACE_ENTRY;
    /* Delete lock used in multi-instanced RPMsg */
    env_delete_mutex(rp_platform_lock);
    rp_platform_lock = ((void *)0);
    RLTRACE_EXIT;
    return 0;
}
