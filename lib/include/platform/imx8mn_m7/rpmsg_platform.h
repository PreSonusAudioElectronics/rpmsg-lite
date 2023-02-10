/*
 * Copyright (c) 2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2022 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef RPMSG_PLATFORM_H_
#define RPMSG_PLATFORM_H_

#include <stdint.h>
#include "MIMX8MN6_cm7.h"

/* RPMSG MU channel index */
// #define RPMSG_MU_CHANNEL (1)

#define RL_N_PLATFORM_CHANS (4U)

/*
 * Linux requires the ALIGN to 0x1000(4KB) instead of 0x80
 */
#ifndef VRING_ALIGN
#define VRING_ALIGN (0x1000U)
#endif

/* contains pool of descriptors and two circular buffers */
#ifndef VRING_SIZE
#define VRING_SIZE (0x8000UL)
#endif

/* define shared memory space for VRINGS per one channel */
#define RL_VRING_OVERHEAD (2UL * VRING_SIZE)

#define RL_GET_VQ_ID(link_id, queue_id) (((queue_id)&0x1U) | (((link_id) << 1U) & 0xFFFFFFFEU))
#define RL_GET_LINK_ID(id)              (((id)&0xFFFFFFFEU) >> 1U)
#define RL_GET_Q_ID(id)                 ((id)&0x1U)

#define RL_PLATFORM_IMX8MN_M7_USER_LINK_ID (0U)
#define RL_PLATFORM_HIGHEST_LINK_ID        (15U)


#ifndef VDEV0_VRING_BASE
#define VDEV0_VRING_BASE	  (0xB8000000U)
#endif

#define RESOURCE_TABLE_OFFSET (0xFF000)

#define RPMSG_LITE_SHMEM_BASE		 (VDEV0_VRING_BASE)
#define RPMSG_LITE_LINK_ID			(RL_PLATFORM_IMX8MN_M7_USER_LINK_ID)

// For environment port
#define MU_IRQ_HANDLER (MU_M7_IRQHandler)

/* platform interrupt related functions */
int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data);
int32_t rp_platform_deinit_interrupt(uint32_t vector_id);

/*!
 * \brief Enable interrupt for a peripheral channel
 * (Peripheral is MU on this platform)
 * 
 * \param channel 
 * \return int32_t 
 */
int32_t rp_platform_interrupt_enable(uint32_t channel);

/*!
 * \brief Disable interrupt for a peripheral channel
 * 
 * \param channel 
 * \return int32_t 
 */
int32_t rp_platform_interrupt_disable(uint32_t channel);

/**
 * rp_platform_in_isr
 *
 * Return whether CPU is processing IRQ
 *
 * @return True for IRQ, false otherwise.
 *
 */
int32_t rp_platform_in_isr(void);

/*!
 * \brief Notifies the other core through the 
 * Messaging Unit that a message is ready
 * 
 * TODO: pass phy_channel instead of environment
 * 
 * \param env 
 * \param vector_id 
 */
void rp_platform_notify(void *env, uint32_t vector_id);

/**
 * rp_platform_time_delay
 *
 * @param num_msec Delay time in ms.
 *
 * This is not an accurate delay, it ensures at least num_msec passed when return.
 */
void rp_platform_time_delay(uint32_t num_msec);

/* platform memory functions */
/**
 * rp_platform_map_mem_region
 *
 * Dummy implementation
 *
 */
void rp_platform_map_mem_region(uintptr_t *vrt_addr, uintptr_t phy_addr, uint32_t size, uint32_t flags);

/**
 * rp_platform_cache_all_flush_invalidate
 *
 * Dummy implementation
 *
 */
void rp_platform_cache_all_flush_invalidate(void);

/**
 * rp_platform_cache_disable
 *
 * Dummy implementation
 *
 */
void rp_platform_cache_disable(void);

/**
 * rp_platform_vatopa
 *
 * Dummy implementation
 *
 */
uint32_t rp_platform_vatopa(void *addr);

/**
 * rp_platform_patova
 *
 * Dummy implementation
 *
 */
void *rp_platform_patova(uint32_t addr);

/* platform init/deinit */

/*!
 * \brief Initialize the hardware platform
 * 
 * \param shmem_addr ptr to start of shared memory area
 * \param phy_channel MU channel to initialize
 * \return int32_t 0 on success, otherwise -1
 */
int32_t rp_platform_init(void *shmem_addr, uint32_t phy_channel);

/**
 * rp_platform_deinit
 *
 * platform/environment deinit process
 * 
 * TODO: Fix me so I'm coherent with multi-environment approach
 */
int32_t rp_platform_deinit(void *shmem_addr);

#endif /* RPMSG_PLATFORM_H_ */
