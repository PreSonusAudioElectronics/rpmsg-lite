/*
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2019 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <string.h>

#include "rpmsg_platform.h"
#include "rpmsg_env.h"
#include <ipm.h>

#if defined(RL_USE_ENVIRONMENT_CONTEXT) && (RL_USE_ENVIRONMENT_CONTEXT == 1)
#error "This RPMsg-Lite port requires RL_USE_ENVIRONMENT_CONTEXT set to 0"
#endif

static int32_t isr_counter     = 0;
static int32_t disable_counter = 0;
static void *rp_platform_lock;
static struct device *ipm_handle = ((void *)0);

void rp_platform_ipm_callback(void *context, u32_t id, volatile void *data)
{
    if (id != RPMSG_MU_CHANNEL)
    {
        return;
    }

    /* Data to be transmitted from Master */
    if (*(uint32_t *)data == 0U)
    {
        env_isr(0);
    }

    /* Data to be received from Master */
    if (*(uint32_t *)data == 0x10000U)
    {
        env_isr(1);
    }
}

static void rp_platform_global_isr_disable(void)
{
    __asm volatile("cpsid i");
}

static void rp_platform_global_isr_enable(void)
{
    __asm volatile("cpsie i");
}

int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
    /* Register ISR to environment layer */
    env_register_isr(vector_id, isr_data);

    env_lock_mutex(rp_platform_lock);

    RL_ASSERT(0 <= isr_counter);

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
    if ((isr_counter == 0) && (ipm_handle != ((void *)0)))
    {
        ipm_set_enabled(ipm_handle, 0);
    }

    /* Unregister ISR from environment layer */
    env_unregister_isr(vector_id);

    env_unlock_mutex(rp_platform_lock);

    return 0;
}

void rp_platform_notify(uint32_t vector_id)
{
    int32_t status;
    switch (RL_GET_LINK_ID(vector_id))
    {
        case RL_rp_platform_IMX7D_M4_LINK_ID:
            env_lock_mutex(rp_platform_lock);
            uint32_t data = (RL_GET_Q_ID(vector_id) << 16);
            RL_ASSERT(ipm_handle);
            do
            {
                status = ipm_send(ipm_handle, 0, RPMSG_MU_CHANNEL, &data, sizeof(uint32_t));
            } while (status == EBUSY);
            env_unlock_mutex(rp_platform_lock);
            return;

        default:
            return;
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
    return (0 != k_is_in_isr());
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

    if ((disable_counter == 0) && (ipm_handle != ((void *)0)))
    {
        ipm_set_enabled(ipm_handle, 1);
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
    if ((disable_counter == 0) && (ipm_handle != ((void *)0)))
    {
        ipm_set_enabled(ipm_handle, 0);
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
    /* Get IPM device handle */
    ipm_handle = device_get_binding(DT_NXP_IMX_MU_MU_B_LABEL);
    if (!ipm_handle)
    {
        return -1;
    }

    /* Register application callback with no context */
    ipm_register_callback(ipm_handle, rp_platform_ipm_callback, ((void *)0));

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
