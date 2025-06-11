/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2023-2025 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __UAPI_MISC_SNPS_ACCEL_H__
#define __UAPI_MISC_SNPS_ACCEL_H__

#include "linux/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SNPS_ACCEL_MAGIC		'N'

#define SNPS_ACCEL_INFO_SHMEM		0x01
#define SNPS_ACCEL_INFO_NOTIFY		0x02
#define SNPS_ACCEL_WAIT_IRQ		0x03
#define SNPS_ACCEL_DMABUF_ALLOC		0x04
#define SNPS_ACCEL_DMABUF_INFO		0x05
#define SNPS_ACCEL_DMABUF_IMPORT	0x06
#define SNPS_ACCEL_DMABUF_DETACH	0x07
#define SNPS_ACCEL_COLLECT_MGR_IRQ	0x08
#define SNPS_ACCEL_PROC_CMD		0x09
#define SNPS_ACCEL_RPM_GET		0x0a
#define SNPS_ACCEL_RPM_PUT		0x0b

#define SNPS_ACCEL_IOCTL_INFO_SHMEM	\
	_IOR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_INFO_SHMEM, struct snps_accel_shmem)
#define SNPS_ACCEL_IOCTL_INFO_NOTIFY	\
	_IOR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_INFO_NOTIFY, struct snps_accel_notify)
#define SNPS_ACCEL_IOCTL_WAIT_IRQ	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_WAIT_IRQ, struct snps_accel_wait_irq)
#define SNPS_ACCEL_IOCTL_DMABUF_ALLOC	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_ALLOC, struct snps_accel_dmabuf_alloc)
#define SNPS_ACCEL_IOCTL_DMABUF_INFO	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_INFO, struct snps_accel_dmabuf_info)
#define SNPS_ACCEL_IOCTL_DMABUF_IMPORT	\
	_IOW(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_IMPORT, struct snps_accel_dmabuf_import)
#define SNPS_ACCEL_IOCTL_DMABUF_DETACH	\
	_IOW(SNPS_ACCEL_MAGIC, SNPS_ACCEL_DMABUF_DETACH, struct snps_accel_dmabuf_detach)
#define SNPS_ACCEL_IOCTL_COLLECT_MGR_IRQ	\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_COLLECT_MGR_IRQ, struct snps_accel_collect_mgr_irq)
#define SNPS_ACCEL_IOCTL_PROC_CMD		\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_PROC_CMD, struct snps_accel_ioctl_proc_cmd)
#define SNPS_ACCEL_IOCTL_RPM_GET		\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_RPM_GET, struct snps_accel_ioctl_rpm_cmd)
#define SNPS_ACCEL_IOCTL_RPM_PUT		\
	_IOWR(SNPS_ACCEL_MAGIC, SNPS_ACCEL_RPM_PUT, struct snps_accel_ioctl_rpm_cmd)

/* Maximum number of processor commands per IOCTL */
#define SNPS_ACCEL_IOCTL_MAX_RPOC_CMDS	16

/* Maximum number of runtime PM cores per IOCTL */
#define SNPS_ACCEL_IOCTL_MAX_RPM_CORES	64

/**
 * enum snaps_accel_ioctl_proc_cmd - SNPS_ACCEL_IOCTL_PROC_CMD command ID.
 * @SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_START: Start of boot a processor.
 * @SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_END: End of boot a processor.
 * @SNPS_ACCEL_IOCTL_PROC_CMD_SHUTDOWN: Shutdown a processor.
 * @SNPS_ACCEL_IOCTL_PROC_CMD_RPM_GET: Get runtime PM usage count for a processor.
 * @SNPS_ACCEL_IOCTL_PROC_CMD_RPM_PUT: Put runtime PM usage count for a processor.
 */
enum snaps_accel_ioctl_proc_cmd {
	SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_START = 0,
	SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_END,
	SNPS_ACCEL_IOCTL_PROC_CMD_SHUTDOWN,
	SNPS_ACCEL_IOCTL_PROC_CMD_RPM_GET,
	SNPS_ACCEL_IOCTL_PROC_CMD_RPM_PUT,
	SNPS_ACCEL_IOCTL_PROC_CMD_NUM,
};

/**
 * struct snps_accel_shmem - SNPS_ACCEL_IOCTL_INFO_SHMEM argument.
 * @offset: Shared memory intermediate offset for use in mmap.
 * @size: Size of mapped region.
 */
struct snps_accel_shmem {
	__u64 offset;
	__u64 size;
};

/**
 * struct snps_accel_notify - SNPS_ACCEL_IOCTL_INFO_NOTIFY argument.
 * @offset: Shared memory intermediate offset for use in mmap.
 * @size: Size of mapped region.
 */
struct snps_accel_notify {
	__u64 offset;
	__u64 size;
};

/**
 * struct snps_accel_wait_irq - SNPS_ACCEL_IOCTL_INFO_NOTIFY argument.
 * @timeout: Timeout in milliseconds for blocking wait operation.
 * @count: Total interrupt count returned by the driver.
 */
struct snps_accel_wait_irq {
	__u32 timeout;
	__u32 count;
};

/**
 * enum - SNPS_ACCEL_IOCTL_DMABUF_ALLOC flags.
 * @SNPS_ACCEL_IO_R: Read.
 * @SNPS_ACCEL_IO_W: Write.
 */
enum {
	SNPS_ACCEL_IO_R = 0x1,
	SNPS_ACCEL_IO_W = 0x2
};

/**
 * struct snps_accel_dmabuf_alloc - SNPS_ACCEL_IOCTL_DMABUF_ALLOC argument.
 * @fd: DMA buffer file descriptor.
 * @flags: Flags to apply for dma buffer device mappings.
 * @size: Size of dma buffer to allocate.
 */
struct snps_accel_dmabuf_alloc {
	__s32 fd;
	__u32 flags;
	__u64 size;
};

/**
 * struct snps_accel_dmabuf_info - SNPS_ACCEL_IOCTL_DMABUF_INFO argument.
 * @fd: DMA buffer file descriptor.
 * @addr: Address as it seen by DMA of a device.
 * @size: Size of dma buffer to allocate.
 */
struct snps_accel_dmabuf_info {
	__s32 fd;
	__u64 addr;
	__u64 size;
};

/**
 * struct snps_accel_dmabuf_import - SNPS_ACCEL_IOCTL_DMABUF_IMPORT argument.
 * @fd: DMA buffer file descriptor of extermal buffer.
 */
struct snps_accel_dmabuf_import {
	__s32 fd;
};

/**
 * struct snps_accel_dmabuf_detach - SNPS_ACCEL_IOCTL_DMABUF_DETACH argument.
 * @fd: DMA buffer file descriptor.
 */
struct snps_accel_dmabuf_detach {
	__s32 fd;
};

/**
 * struct snps_accel_collect_mgr_irq - SNPS_ACCEL_IOCTL_COLLECT_MGR_IRQ argument.
 * @index: Index of IRQ(s) provided by an accelerator manager.
 * @timeout_ms: Timeout in milliseconds.
 * @count: Total interrupt count returned by the driver.
 */
struct snps_accel_collect_mgr_irq {
	__u32 index;
	__u32 timeout_ms;
	__u32 count;
};

/**
 * struct snps_accel_ioctl_proc_cmd - SNPS_ACCEL_IOCTL_PROC_CMD argument.
 * @cmd_id: Processor command ID.
 * @num_procs: The number of processor(s) needing the command.
 * @procs: Processor ID & result of command.
 */
struct snps_accel_ioctl_proc_cmd {
	__u32 cmd_id;
	__u32 num_procs;
	struct {
		__u32 processor_id;
		__s32 result;
	} procs[SNPS_ACCEL_IOCTL_MAX_RPOC_CMDS];
};

/**
 * struct snps_accel_ioctl_rpm_cmd - SNPS_ACCEL_IOCTL_RPM_GET/PUT argument.
 * @num_cores: The number of core(s) needing the command.
 * @core_id_bw: Bit-width of core ID.
 * @cores: Processor core ID & result of command.
 */
struct snps_accel_ioctl_rpm_cmd {
	__u32 num_cores;
	__u32 core_id_bw;
	struct {
		__u32 id;
		__s32 result;
	} cores[SNPS_ACCEL_IOCTL_MAX_RPM_CORES];
};

#if defined(__cplusplus)
}
#endif

#endif  /* __UAPI_MISC_SNPS_ACCEL_H__ */
