#ifndef _rpmsg_rp_platform_h
#define _rpmsg_rp_platform_h

#include <stdint.h>
#include <arch/arch_ops.h>
#include <MIMX8MN6_ca53.h>

/* RPMSG MU channel index */
// TODO: fixme
#define RPMSG_MU_CHANNEL (1)

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

/* size of shared memory + 2*VRING size */
#define RL_VRING_OVERHEAD (2UL * VRING_SIZE)

#define RL_GET_VQ_ID(link_id, queue_id) (((queue_id)&0x1U) | (((link_id) << 1U) & 0xFFFFFFFEU))
#define RL_GET_LINK_ID(id)              (((id)&0xFFFFFFFEU) >> 1U)
#define RL_GET_Q_ID(id)                 ((id)&0x1U)

#define RL_rp_platform_HIGHEST_LINK_ID        (15U)

#ifndef VDEV0_VRING_BASE
#define VDEV0_VRING_BASE	  (0xB8000000U)
#endif

#define RESOURCE_TABLE_OFFSET (0xFF000)

// TODO: fix me
#define RPMSG_LITE_SHMEM_BASE		 (VDEV0_VRING_BASE)

/* platform interrupt related functions */
int32_t rp_platform_init_interrupt(uint32_t vector_id, void *isr_data);
int32_t rp_platform_deinit_interrupt(uint32_t vector_id);
int32_t rp_platform_interrupt_enable(uint32_t vector_id);
int32_t rp_platform_interrupt_disable(uint32_t vector_id);
int32_t rp_platform_in_isr(void);
void rp_platform_notify(uint32_t vector_id);

/* platform low-level time-delay (busy loop) */
void rp_platform_time_delay(uint32_t num_msec);

/* platform memory functions */
void rp_platform_map_mem_region(uint32_t vrt_addr, uint32_t phy_addr, uint32_t size, uint32_t flags);
void rp_platform_cache_all_flush_invalidate(void);
void rp_platform_cache_disable(void);
uintptr_t rp_platform_vatopa(void *addr);
void *rp_platform_patova(uintptr_t addr);

/* platform init/deinit */
int32_t rp_platform_init(void *shmem_addr);
int32_t rp_platform_deinit(void);

static inline void rp_platform_global_isr_disable(void)
{
    arch_disable_ints();
}

static inline void rp_platform_global_isr_enable(void)
{
    arch_enable_ints();
}

#endif // _rpmsg_rp_platform_h