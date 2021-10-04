#include <stdio.h>
#include <string.h>
#include <arch/arch_ops.h>
#include <arch/ops.h>
#include <dev/interrupt.h>
#include <sys/types.h>
#include "rpmsg_platform.h"
#include "rpmsg_env.h"
#include "rsc_table.h"

#include "fsl_device_registers.h"
#include <fsl_mu.h>

#include <MIMX8MN6_ca53.h>

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

#define APP_MU_IRQ_PRIORITY (3U)

static int32_t isr_counter     = 0;
static int32_t disable_counter = 0;
static void *rp_platform_lock;

int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
    /* Register ISR to environment layer */
    env_register_isr(vector_id, isr_data);

    /* Prepare the MU Hardware, enable channel 1 interrupt */
    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 <= isr_counter);
    if (isr_counter == 0)
    {
        MU_EnableInterrupts(MUB, (1UL << 27U) >> RPMSG_MU_CHANNEL);
    }
    isr_counter++;

    env_unlock_mutex(rp_platform_lock);

    return 0;
}

int32_t rp_platform_deinit_interrupt(uint32_t vector_id)
{
    /* Prepare the MU Hardware */
    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 < isr_counter);
    isr_counter--;
    if (isr_counter == 0)
    {
        MU_DisableInterrupts(MUB, (1UL << 27U) >> RPMSG_MU_CHANNEL);
    }

    /* Unregister ISR from environment layer */
    env_unregister_isr(vector_id);

    env_unlock_mutex(rp_platform_lock);

    return 0;
}

void rp_platform_notify(uint32_t vector_id)
{
    /* As Linux suggests, use MU->Data Channel 1 as communication channel */
    uint32_t msg = (uint32_t)(vector_id << 16);

    env_lock_mutex(rp_platform_lock);
    MU_SendMsg(MUB, RPMSG_MU_CHANNEL, msg);
    env_unlock_mutex(rp_platform_lock);
}

/*
 * MU Interrrupt RPMsg handler
 */
enum handler_return MU_IRQHandler(void* context)
{
    uint32_t channel;

    if ((((1UL << 27U) >> RPMSG_MU_CHANNEL) & MU_GetStatusFlags(MUB)) != 0UL)
    {
        channel = MU_ReceiveMsgNonBlocking(MUB, RPMSG_MU_CHANNEL); // Read message from RX register.
        env_isr(channel >> 16);
    }

    // should we put a memory barrier here?

    return INT_RESCHEDULE;
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
    uint32_t loop;

    /* Recalculate the CPU frequency */
    SystemCoreClockUpdate();

    /* Calculate the CPU loops to delay, each loop has 3 cycles */
    loop = SystemCoreClock / 3U / 1000U * num_msec;

    /* There's some difference among toolchains, 3 or 4 cycles each loop */
    while (loop > 0U)
    {
        __NOP();
        loop--;
    }
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

    if (disable_counter == 0)
    {
        // NVIC_EnableIRQ(MU_M7_IRQn);
        // TODO: fixme
    }
    rp_platform_global_isr_enable();
    return ((int32_t)vector_id);
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
    RL_ASSERT(0 <= disable_counter);

    rp_platform_global_isr_disable();
    /* virtqueues use the same NVIC vector
       if counter is set - the interrupts are disabled */
    if (disable_counter == 0)
    {
        // NVIC_DisableIRQ(MU_M7_IRQn);
        // TODO: fixme
    }
    disable_counter++;
    rp_platform_global_isr_enable();
    return ((int32_t)vector_id);
}

/**
 * rp_platform_map_mem_region
 *
 * Dummy implementation
 *
 */
void rp_platform_map_mem_region(uint32_t vrt_addr, uint32_t phy_addr, uint32_t size, uint32_t flags)
{
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
    return ((uintptr_t)(char *)addr);
}

/**
 * rp_platform_patova
 *
 * Dummy implementation
 *
 */
void *rp_platform_patova(uintptr_t addr)
{
    return ((void *)(char *)addr);
}

/**
 * rp_platform_init
 *
 * platform/environment init
 */
int32_t rp_platform_init(void *shmem_addr)
{
    copyResourceTable(shmem_addr);

    /*
     * Prepare for the MU Interrupt
     *  MU must be initialized before rpmsg init is called
     */
    MU_Init(MUB);

    // NVIC_SetPriority(MU_M7_IRQn, APP_MU_IRQ_PRIORITY);
    // NVIC_EnableIRQ(MU_M7_IRQn);

    /* Create lock used in multi-instanced RPMsg */
    if (0 != env_create_mutex(&rp_platform_lock, 1))
    {
        return -1;
    }

    return 0;
}

/**
 * rp_platform_deinit
 *
 * platform/environment deinit process
 */
int32_t rp_platform_deinit(void)
{
    /* Delete lock used in multi-instanced RPMsg */
    env_delete_mutex(rp_platform_lock);
    rp_platform_lock = ((void *)0);
    return 0;
}