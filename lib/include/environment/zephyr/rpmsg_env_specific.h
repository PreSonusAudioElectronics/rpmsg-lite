/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**************************************************************************
 * FILE NAME
 *
 *       rpmsg_env_specific.h
 *
 * DESCRIPTION
 *
 *       This file contains Zephyr specific constructions.
 *
 **************************************************************************/
#ifndef RPMSG_ENV_SPECIFIC_H_
#define RPMSG_ENV_SPECIFIC_H_

#include <zephyr/kernel.h>
#include "rpmsg_default_config.h"

/* Max supported ISR counts */
#define ISR_COUNT (32U)
/*!
 * Structure to keep track of registered ISR's.
 */
typedef struct isr_info
{
    void *data;
} rl_isr_info_t;

typedef struct rpmsg_queue_rx_cb_data_
{
    uint32_t src;
    void *data;
    uint32_t len;
} rpmsg_queue_rx_cb_data_t;


typedef struct rl_zephyr_env_
{
    void* shmem_va;
    uint32_t shmem_size;
    void* sgi_mbox_va;
    uint32_t sgi_mbox_size;
    void* vring_buf_va;
    uint32_t vring_buf_size;
    uint16_t phy_channel;
    struct k_spinlock spinlock;
    k_spinlock_key_t key;
    struct k_event tx_events;
    struct k_sem sema;
    uint16_t init_counter;
    bool initialized;
    rl_isr_info_t isr_table [ISR_COUNT];
} rl_env_t;

typedef struct rl_zephyr_env_cfg_
{
    uintptr_t shmem_pa;
    uint32_t shmem_size;
    uintptr_t sgi_mbox_pa;
    uint32_t sgi_mbox_size;
    uint32_t sgi_mbox_irq_num;
    uintptr_t vring_buf_pa;
    uint32_t vring_buf_size;
    uint16_t phy_channel;
} rl_env_cfg_t;


#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
#include <zephyr.h>

typedef k_sem LOCK_STATIC_CONTEXT;
typedef k_msgq rpmsg_static_queue_ctxt;
/* Queue object static storage size in bytes, should be defined as (RL_BUFFER_COUNT*sizeof(rpmsg_queue_rx_cb_data_t)) */
#define RL_ENV_QUEUE_STATIC_STORAGE_SIZE (RL_BUFFER_COUNT * sizeof(rpmsg_queue_rx_cb_data_t))
#endif

#endif /* RPMSG_ENV_SPECIFIC_H_ */
