/*
 * Copyright 2019-2020 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "rpmsg_platform.h"
#include "rpmsg_env.h"
#include <xtensa/config/core.h>

#ifndef FSL_RTOS_FREE_RTOS
#include <xtensa/xos.h>
#endif

#include "fsl_device_registers.h"
#include "fsl_mu.h"

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

static int32_t isr_counter     = 0;
static int32_t disable_counter = 0;
static void *rp_platform_lock;

void MU_B_IRQHandler(void *arg)
{
    uint32_t flags;
    flags = MU_GetStatusFlags(MUB);
    if (((uint32_t)kMU_GenInt0Flag & flags) != 0UL)
    {
        MU_ClearStatusFlags(MUB, (uint32_t)kMU_GenInt0Flag);
        env_isr(0);
    }
    if (((uint32_t)kMU_GenInt1Flag & flags) != 0UL)
    {
        MU_ClearStatusFlags(MUB, (uint32_t)kMU_GenInt1Flag);
        env_isr(1);
    }
}

int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
    /* Register ISR to environment layer */
    env_register_isr(vector_id, isr_data);

    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 <= isr_counter);

    if (isr_counter < 2)
    {
        MU_EnableInterrupts(MUB, 1UL << (31UL - vector_id));
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

    if (isr_counter < 2)
    {
        MU_DisableInterrupts(MUB, 1UL << (31UL - vector_id));
    }

    /* Unregister ISR from environment layer */
    env_unregister_isr(vector_id);

    env_unlock_mutex(rp_platform_lock);

    return 0;
}

void rp_platform_notify(uint32_t vector_id)
{
    env_lock_mutex(rp_platform_lock);
    (void)MU_TriggerInterrupts(MUB, 1UL << (19UL - RL_GET_Q_ID(vector_id)));
    env_unlock_mutex(rp_platform_lock);
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

    /* Calculate the CPU loops to delay, each loop has approx. 6 cycles */
    loop = SystemCoreClock / 6U / 1000U * num_msec;

    while (loop > 0U)
    {
        asm("nop");
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
    return (xthal_get_interrupt() & xthal_get_intenable());
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

#ifdef FSL_RTOS_FREE_RTOS
    xt_interrupt_enable(6);
#else
    xos_interrupt_enable(6);
#endif
    disable_counter--;
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

#ifdef FSL_RTOS_FREE_RTOS
    xt_interrupt_disable(6);
#else
    xos_interrupt_disable(6);
#endif
    disable_counter++;
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
}

/**
 * rp_platform_vatopa
 *
 * Dummy implementation
 *
 */
uint32_t rp_platform_vatopa(void *addr)
{
    return ((uint32_t)(char *)addr);
}

/**
 * rp_platform_patova
 *
 * Dummy implementation
 *
 */
void *rp_platform_patova(uint32_t addr)
{
    return ((void *)(char *)addr);
}

/**
 * rp_platform_init
 *
 * platform/environment init
 */
int32_t rp_platform_init(void)
{
    /* Create lock used in multi-instanced RPMsg */
    if (0 != env_create_mutex(&rp_platform_lock, 1))
    {
        return -1;
    }

    /* Register interrupt handler for MU_B on HiFi4 */
#ifdef FSL_RTOS_FREE_RTOS
    xt_set_interrupt_handler(6, MU_B_IRQHandler, ((void *)0));
#else
    xos_register_interrupt_handler(6, MU_B_IRQHandler, ((void *)0));
#endif

    return 0;
}

/**
 * rp_platform_deinit
 *
 * platform/environment deinit process
 */
int32_t rp_platform_deinit(void)
{
#ifdef FSL_RTOS_FREE_RTOS
    xt_set_interrupt_handler(6, ((void *)0), ((void *)0));
#else
    xos_register_interrupt_handler(6, ((void *)0), ((void *)0));
#endif

    /* Delete lock used in multi-instanced RPMsg */
    env_delete_mutex(rp_platform_lock);
    rp_platform_lock = ((void *)0);
    return 0;
}
