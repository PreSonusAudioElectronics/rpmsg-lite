/*
 * Copyright 2017-2020 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "rpmsg_platform.h"
#include "rpmsg_env.h"
#include "rsc_table.h"
#include "rpmsg_config.h"
#include "rpmsg_trace.h"

#include "fsl_device_registers.h"
#include "fsl_mu.h"
#include "MIMX8MN6_cm7.h"

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 0)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 1"
#endif

/////////////////////////////////////////////////////////////////////////////////
// Declaring these here because they are unique to imx8mn_cm7 / zephyr combo
int env_get_channel( void *env );
void *env_get_env(uint32_t channel);

/////////////////////////////////////////////////////////////////////////////////

#define APP_MU_IRQ_PRIORITY (3U)
#define RL_MASK_MU_CHAN0_IRQ (1UL << 27U)

static inline uint32_t getRxChanMask(uint32_t channel)
{
    RL_ASSERT( channel < RL_N_PLATFORM_CHANS );
    return ( kMU_Rx0FullFlag >> channel );
}

static inline uint32_t getTxChanMask(uint32_t channel)
{
    RL_ASSERT( channel < RL_N_PLATFORM_CHANS );
    return ( kMU_Tx0EmptyFlag >> channel );
}

static int32_t disable_counter = 0;
static void *rp_platform_lock;


static void rp_platform_global_isr_disable(void)
{
    __asm volatile("cpsid i");
}

static void rp_platform_global_isr_enable(void)
{
    __asm volatile("cpsie i");
}

void rp_platform_notify(void *env, uint32_t vector_id)
{
    uint32_t channel = env_get_channel(env);
    env_lock_mutex(rp_platform_lock);
    uint32_t msg = (uint32_t)(vector_id << 16);
    MU_SendMsg(MUB, channel, msg);
    env_unlock_mutex(rp_platform_lock);
}

/*
 * MU Interrrupt RPMsg handler
 */
int32_t MU_M7_IRQHandler(void)
{
    uint32_t vector;
    MU_Type *base = MUB;
    for( uint32_t channel=0; channel<RL_N_PLATFORM_CHANS; ++channel )
    {
        // Rx for this channel
        uint32_t mask = getRxChanMask(channel);
        if ( (mask & MU_GetStatusFlags(base)) != 0UL )
        {
            vector = MU_ReceiveMsgNonBlocking(base, channel); // Read message from RX register.
            env_isr( env_get_env(channel), vector >> 16);
            MU_ClearStatusFlags(base, mask);
        }

        // Tx for this channel
        mask = getTxChanMask(channel);
        if ( (mask & MU_GetStatusFlags(base)) != 0 )
        {
            MU_ClearStatusFlags(base, mask);
        }
    }

    /* ARM errata 838869, affects Cortex-M4, Cortex-M4F Store immediate overlapping
     * exception return operation might vector to incorrect interrupt.
     * For Cortex-M7, if core speed much faster than peripheral register write speed,
     * the peripheral interrupt flags may be still set after exiting ISR, this results to
     * the same error similar with errata 83869 */
#if (defined __CORTEX_M) && ((__CORTEX_M == 4U) || (__CORTEX_M == 7U))
    __DSB();
#endif

    return 0;
}

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

int32_t rp_platform_in_isr(void)
{
    return (((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0UL) ? 1 : 0);
}

int32_t rp_platform_interrupt_enable(uint32_t channel)
{
    RL_ASSERT(0 < disable_counter);

    rp_platform_global_isr_disable();
    disable_counter--;

    if (disable_counter == 0)
    {
        RLTRACEF("enable MU Ch %d RX Irq\n", channel);
        uint32_t mask = ( kMU_Rx0FullInterruptEnable >> channel );
        MU_EnableInterrupts(MUB, mask );
    }
    rp_platform_global_isr_enable();
    return ((int32_t)channel);
}

int32_t rp_platform_interrupt_disable(uint32_t channel)
{
    RL_ASSERT(0 <= disable_counter);

    rp_platform_global_isr_disable();
    /* virtqueues use the same NVIC vector
       if counter is set - the interrupts are disabled */
    if (disable_counter == 0)
    {
        RLTRACEF("disable MU Ch %d RX Irq\n", channel);
        MU_DisableInterrupts( MUB, ( kMU_Rx0FullInterruptEnable >> channel ) );      
    }
    disable_counter++;
    rp_platform_global_isr_enable();
    return ((int32_t)channel);
}


int32_t rp_platform_init(void *shmem_addr, uint32_t phy_channel)
{
    if( phy_channel >= MU_TR_COUNT )
    {
        return -1;
    }

    copyResourceTable(shmem_addr);

    static uint32_t const irqEnableMasks[] = {
        ( kMU_Tx0EmptyInterruptEnable | kMU_Rx0FullInterruptEnable | kMU_GenInt0InterruptEnable ),
        ( kMU_Tx1EmptyInterruptEnable | kMU_Rx1FullInterruptEnable | kMU_GenInt1InterruptEnable ),
        ( kMU_Tx2EmptyInterruptEnable | kMU_Rx2FullInterruptEnable | kMU_GenInt2InterruptEnable ),
        ( kMU_Tx3EmptyInterruptEnable | kMU_Rx3FullInterruptEnable | kMU_GenInt3InterruptEnable )
    };

    static uint32_t const irqFlagMasks[] = {
        ( kMU_Tx0EmptyFlag | kMU_Rx0FullFlag | kMU_GenInt0Flag ),
        ( kMU_Tx1EmptyFlag | kMU_Rx1FullFlag | kMU_GenInt1Flag ),
        ( kMU_Tx2EmptyFlag | kMU_Rx2FullFlag | kMU_GenInt2Flag ),
        ( kMU_Tx3EmptyFlag | kMU_Rx3FullFlag | kMU_GenInt3Flag )
    };

    /*
     * Prepare for the MU Interrupt
     *  MU must be initialized before rpmsg init is called
     */
    MU_Init(MUB);
    NVIC_SetPriority(MU_M7_IRQn, APP_MU_IRQ_PRIORITY);

    /*
     * Disable all MU interrupts
     * These will be enabled individually as needed
     */
    MU_DisableInterrupts( MUB, irqEnableMasks[phy_channel] );

    /*
     * Clear any pending MU interrupts
     */
    MUB->SR |= irqFlagMasks[phy_channel];

    /* Create lock used in multi-instanced RPMsg */
    if (0 != env_create_mutex(&rp_platform_lock, 1))
    {
        return -1;
    }

    return 0;
}

void rp_platform_map_mem_region(uintptr_t *vrt_addr, uintptr_t phy_addr, uint32_t size, uint32_t flags)
{
}

void rp_platform_cache_all_flush_invalidate(void)
{
}

void rp_platform_cache_disable(void)
{
}

uint32_t rp_platform_vatopa(void *addr)
{
    return ((uint32_t)(char *)addr);
}

void *rp_platform_patova(uint32_t addr)
{
    return ((void *)(char *)addr);
}

int32_t rp_platform_deinit(void *shared_mem_addr)
{
    /* Delete lock used in multi-instanced RPMsg */
    env_delete_mutex(rp_platform_lock);
    rp_platform_lock = ((void *)0);
    return 0;
}


#if 0
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

int32_t rp_platform_deinit_interrupt(void *env, uint32_t vector_id)
{
    RL_ASSERT( env );
    uint32_t channel = env_get_channel( env );

    /* Prepare the MU Hardware */
    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 < isr_counter);
    isr_counter--;
    if (isr_counter == 0)
    {
        MU_DisableInterrupts(MUB, getChanMask(channel) );
    }

    /* Unregister ISR from environment layer */
    env_unregister_isr(env, vector_id);

    env_unlock_mutex(rp_platform_lock);

    return 0;
}
#endif

