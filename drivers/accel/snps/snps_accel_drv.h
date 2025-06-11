/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __SNPS_ACCEL_DRV_H__
#define __SNPS_ACCEL_DRV_H__

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mempool.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/snps_accel.h>
#include <linux/kref.h>
#include <uapi/misc/snps_accel.h>

#define SNPS_ACCEL_DRV_MATCH_ARCSYNC	"snps,accel-arcsync"
#define SNPS_ACCEL_DRV_MATCH_SYSTEM	"snps,accel-system"
#define SNPS_ACCEL_DRV_MATCH_NPX6_V1	"snps,accel-npx6-v1"
#define SNPS_ACCEL_DRV_MATCH_NPX6_V2	"snps,accel-npx6-v2"
#define SNPS_ACCEL_DRV_MATCH_NPX6_V2p1	"snps,accel-npx6-v2.1"
#define SNPS_ACCEL_DRV_MATCH_VPX5	"snps,accel-vpx5"
#define SNPS_ACCEL_MAX_PROCESSORS_PS	16
#define SNPS_ACCEL_MAX_CORES_PP		32
#define SNPS_ACCEL_MIN_CORES_PP		1
#define SNPS_ACCEL_NPX_MAX_GROUPS	4
#define SNPS_ACCEL_NPX_MAX_CORES_PG	4
#define SNPS_ACCEL_NPX_MAX_CORES	26
#define SNPS_ACCEL_NPX_MAX_L2CORES	2
#define SNPS_ACCEL_VPX_MAX_CORES	4
#define SNPS_ACCEL_MAX_MGR_IRQS		4
#define SNPS_ACCEL_MAX_MGR_IRQ_TUPLES	16
#define SNPS_ACCEL_CORE_DEV_NAME_PREFIX	"snps_accel_core"

extern struct class *snps_accel_class;

struct snps_accel_mgr;
struct snps_accel_system;
struct snps_accel_processor;

/**
 * enum snps_accel_dev_type - Device type.
 * @SNPS_ACCEL_DEV_TYPE_ARCSYNC: ARCSync.
 * @SNPS_ACCEL_DEV_TYPE_NPX6_V1: NPX6 v1 processor.
 * @SNPS_ACCEL_DEV_TYPE_NPX6_V2: NPX6 v2.0 processor.
 * @SNPS_ACCEL_DEV_TYPE_NPX6_V2p1: NPX6 v2.1 processor.
 * @SNPS_ACCEL_DEV_TYPE_NPX6_V2p2: NPX6 v2.2 processor.
 * @SNPS_ACCEL_DEV_TYPE_NPX_END: End of NPX processor.
 * @SNPS_ACCEL_DEV_TYPE_VPX5: VPX5 processor.
 * @SNPS_ACCEL_DEV_TYPE_VPX_END: End of VPX processor.
 * @SNPS_ACCEL_DEV_TYPE_SYSTEM: System.
 * @SNPS_ACCEL_DEV_TYPE_CORE: Core.
 */
enum snps_accel_dev_type {
	SNPS_ACCEL_DEV_TYPE_ARCSYNC = 0,
	SNPS_ACCEL_DEV_TYPE_NPX6_V1 = 10,
	SNPS_ACCEL_DEV_TYPE_NPX6_V2,
	SNPS_ACCEL_DEV_TYPE_NPX6_V2p1,
	SNPS_ACCEL_DEV_TYPE_NPX6_V2p2,
	SNPS_ACCEL_DEV_TYPE_NPX_END,
	SNPS_ACCEL_DEV_TYPE_VPX5 = 30,
	SNPS_ACCEL_DEV_TYPE_VPX_END,
	SNPS_ACCEL_DEV_TYPE_SYSTEM = 40,
	SNPS_ACCEL_DEV_TYPE_CORE,
};

/**
 * struct snps_accel_dev_data - Device data.
 * @type: Type.
 * @probe: Probe operation.
 * @remove: Remove operation.
 */
struct snps_accel_dev_data {
	enum snps_accel_dev_type type;
	int (*probe)(struct platform_device *pdev, const void *data);
	void (*remove)(struct platform_device *pdev);
};

/**
 * struct snps_accel_mgr_irq_tuple - Tuple of a manager IRQ.
 * @irq_link: Link to tuples list.
 * @refcount: Reference count.
 * @handler: Manager IRQ handler.
 * @data: Private data.
 */
struct snps_accel_mgr_irq_tuple {
	struct list_head irq_link;
	refcount_t refcount;
	snps_accel_mgr_irq_handler handler;
	void *data;
};

/**
 * struct snps_accel_mgr_irq - Manager IRQ.
 * @mgr: Manager.
 * @index: Index of the manager IRQ.
 * @irq_id: ID in interrupt vector table of host processor.
 * @tuples_lock: Lock of tuples list.
 * @tuples: Tuples list.
 * @num_tuples: Number of tuples.
 * @free_work: Deferred work to free tuples.
 * @free_tuples: Tuples to be freed.
 */
struct snps_accel_mgr_irq {
	struct snps_accel_mgr *mgr;
	u32 index;
	int irq_id;
	spinlock_t tuples_lock;
	struct list_head tuples;
	u32 num_tuples;
	struct work_struct free_work;
	struct list_head free_tuples;
};

/**
 * struct snps_accel_arcsync - Private data of ARCSync.
 * @mgr: Manager.
 * @version: Version.
 * @num_processors: Number of processors.
 * @max_cores_per_processor: Maximum number of core per processor.
 * @max_vpx_blocks_per_processor: Maximum number of vpx blocks per processor.
 * @coreid_bw: Bit-width of core ID.
 * @has_pmu: Has PMU or not.
 */
struct snps_accel_arcsync {
	struct snps_accel_mgr *mgr;
	u32 version;
	u32 num_processors;
	u32 max_cores_per_processor;
	u32 num_vpx_processors;
	u32 max_blocks_per_vpx_processor;
	u32 num_vpx_blocks;
	u32 coreid_bw;
	bool has_pmu;
};

/**
 * struct snps_accel_mgr - Manager.
 * @genpd: Generic PM domain.
 * @data: Match data.
 * @dev: Device.
 * @priv: Private data.
 * @ops: Operations.
 * @custom_ops: Users-custom operations.
 * @custom_ops_data: Private data of users-custom operations.
 * @irqs: IRQs.
 * @num_irqs: Number of IRQs.
 * @host_processor_id: Host processor ID.
 * @host_core_id: Host core ID.
 * @mgr_id: ID.
 * @reg_lock: Registers lock.
 * @reg_vbase: Host virtual base address of registers.
 * @reg_pbase: Host physical base address of registers.
 * @reg_size: Size of registers.
 */
struct snps_accel_mgr {
	struct generic_pm_domain genpd;
	const struct snps_accel_dev_data *data;
	struct device *dev;
	void *priv;
	const struct snps_accel_mgr_ops *ops;
	struct snps_accel_custom_ops *custom_ops;
	void *custom_ops_data;
	struct snps_accel_mgr_irq irqs[SNPS_ACCEL_MAX_MGR_IRQS];
	u32 num_irqs;
	u32 host_processor_id;
	u32 host_core_id;
	u32 mgr_id;
	spinlock_t reg_lock;
	void __iomem *reg_vbase;
	phys_addr_t reg_pbase;
	resource_size_t reg_size;
	bool power_always_on;
};

/**
 * struct snps_accel_mbox_msg - Mailbox message.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 */
struct snps_accel_mbox_msg {
	u32 processor_id;
	u32 core_id;
};

/**
 * struct snps_accel_mem_ctx - Memory context
 * @dev: Device.
 * @list_lock: Memory list lock.
 * @mlist: Memory list.
 */
struct snps_accel_mem_ctx {
	struct device *dev;
	struct mutex list_lock;
	struct list_head mlist;
};

/**
 * struct snps_accel_dmabuf_attachment - DMA buffer attachment.
 * @dev: Device.
 * @sgt: Scatter-gather table.
 * @mapped: Mapped or not.
 * @node: List head.
 */
struct snps_accel_dmabuf_attachment {
	struct device *dev;
	struct sg_table sgt;
	bool mapped;
	struct list_head node;
};

/**
 * struct snps_accel_mem_buffer - Memory buffer.
 * @ctx: Memory context.
 * @ctx_link: Memory context list head.
 * @dev: Device.
 * @dmabuf: DMA buffer.
 * @dma_dir: Data direction.
 * @fd: File descriptor.
 * @da: Device (physical) address.
 * @va: Host virtual address.
 * @pa: Host physcial address.
 * @size: Size.
 * @mapped: Mapped or not.
 * @dmasgt: Scatter-gather table.
 * @import_attach: Import attachment.
 * @lock: Lock of attachments list.
 * @attachments: Attachments list
 */
struct snps_accel_mem_buffer {
	struct snps_accel_mem_ctx *ctx;
	struct list_head ctx_link;
	struct device *dev;
	struct dma_buf *dmabuf;
	enum dma_data_direction dma_dir;
	int fd;
	dma_addr_t da;
	void *va;
	phys_addr_t pa;
	size_t size;
	bool mapped;
	struct sg_table *dmasgt;
	struct dma_buf_attachment *import_attach;
	struct mutex lock;
	struct list_head attachments;
};

/**
 * enum snps_accel_proc_mem_type - Type of memory region.
 * @SNPS_ACCEL_PROC_MEM_TYPE_FWMEM: Firmware memory region.
 * @SNPS_ACCEL_PROC_MEM_TYPE_FWVMEM: Firmware virtual memory region.
 * @SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG: NPX direct memory interface configure region.
 * @SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI: NPX DMI region.
 * @SNPS_ACCEL_PROC_MEM_TYPE_UNKNOWN: Unknown memory region.
 */
enum snps_accel_proc_mem_type {
	SNPS_ACCEL_PROC_MEM_TYPE_FWMEM = 0,
	SNPS_ACCEL_PROC_MEM_TYPE_FWVMEM,
	SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG,
	SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI,
	SNPS_ACCEL_PROC_MEM_TYPE_UNKNOWN,
	SNPS_ACCEL_PROC_MEM_TYPE_NUM,
};

/**
 * struct snps_accel_proc_mem - Memory region.
 * @virt_addr: Virtual address of the memory region from host view.
 * @phys_addr: Physical address of the memory region from host view.
 * @dev_addr: Device (physical) address of the memory region from device view.
 * @size: Size of the memory region.
 * @type: Type of the memory region.
 * @is_ram: REGION_DISJOINT, REGION_INTERSECTS, or REGION MIXED with any region matching flag/desc on calling region_intersects().
 */
struct snps_accel_proc_mem {
	union {
		void *mem;
		void __iomem *iomem;
	} virt_addr;
	phys_addr_t phys_addr;
	u64 dev_addr;
	resource_size_t size;
	enum snps_accel_proc_mem_type type;
	int is_ram;
};

/**
 * struct snps_accel_ctrl - Control of a system.
 * @cdev: Charactor device.
 * @cdev_id: Device number of charactor device.
 * @cdev_dev: Device of charactor device.
 * @open_count: Open count of charactor device file.
 * @open_count_wq: Wait queue of open count.
 */
struct snps_accel_ctrl {
	struct cdev cdev;
	dev_t cdev_id;
	struct device *cdev_dev;
	atomic_t open_count;
	wait_queue_head_t open_count_wq;
};

/**
 * struct snps_accel_system - A system.
 * @refcount: Reference count.
 * @data: Match data.
 * @dev: Device.
 * @mgr: Mananger of the system.
 * @irq_counts: Count of each manager IRQ.
 * @irq_waitq: Wait queue for collecting count of each manager IRQ.
 * @procs_lock: Lock of processors array.
 * @procs: Processors array.
 * @ctrl: Control.
 * @mbox: Mailbox.
 * @num_procs: Number of processors.
 * @system_id: ID.
 * @host_shrmem_pbase: Host-view physical base address of host shared memory.
 * @host_shrmem_dbase: Device-view physical base address of host shared memory.
 * @host_shrmem_size: Size of host shared memory.
 * @dev_shrmem_dbase: Device-view physical base address of device shared memory.
 * @dev_shrmem_size: Size of device shared memory.
 * @mgr_reg_pbase: Host physical base address of manager's registers.
 * @mgr_reg_size: Size of manager's registers.
 */
struct snps_accel_system {
	struct kref refcount;
	const struct snps_accel_dev_data *data;
	struct device *dev;
	struct snps_accel_mgr *mgr;
	atomic_t irq_counts[SNPS_ACCEL_MAX_MGR_IRQS];
	wait_queue_head_t irq_waitq;
	struct rw_semaphore procs_lock;
	struct snps_accel_processor *procs[SNPS_ACCEL_MAX_PROCESSORS_PS];
	struct snps_accel_ctrl ctrl;
	u32 num_procs;
	u32 system_id;
	phys_addr_t host_shrmem_pbase;
	u64 host_shrmem_dbase;
	resource_size_t host_shrmem_size;
	u64 dev_shrmem_dbase;
	resource_size_t dev_shrmem_size;
	phys_addr_t mgr_reg_pbase;
	resource_size_t mgr_reg_size;
};

/**
 * struct snps_accel_ctrl_priv - Control private data of a system.
 * @ref: Reference count.
 * @system: System.
 * @mem: Memory region context.
 * @handled_irq_counts: Count of handled manager IRQs.
 */
struct snps_accel_ctrl_priv {
	struct kref ref;
	struct snps_accel_system *system;
	struct snps_accel_mem_ctx mem;
	u32 handled_irq_counts[SNPS_ACCEL_MAX_MGR_IRQS];
};

/**
 * enum snps_accel_fw_type - Type of firmware.
 * @SNPS_ACCEL_FW_TYPE_CORE: Firmware of (L1) cores.
 * @SNPS_ACCEL_FW_TYPE_L2_CORE: Firmware of L2 cores.
 */
enum snps_accel_fw_type {
	SNPS_ACCEL_FW_TYPE_CORE = 0,
	SNPS_ACCEL_FW_TYPE_L2_CORE,
	SNPS_ACCEL_FW_TYPE_NUM,
};

/**
 * snps_accel_get_fw_type_string - Get string of firmware type.
 * @type: Type of firmware.
 *
 * Return: String of firmware type
 */
static inline char *snps_accel_get_fw_type_string(enum snps_accel_fw_type type)
{
	if (type == SNPS_ACCEL_FW_TYPE_CORE)
		return "core";
	else if (type == SNPS_ACCEL_FW_TYPE_L2_CORE)
		return "l2_core";
	else
		return "unkonwn";
}

/**
 * struct snps_accel_core - Core.
 * @genpd: Generic PM domain.
 * @pdev: Platform device.
 * @proc: Processor to which a core belongs.
 * @rproc: Remote processor to which a core refers.
 * @core_id: ID.
 * @unused: Unused or not.
 */
struct snps_accel_core {
	struct generic_pm_domain genpd;
	struct platform_device *pdev;
	struct snps_accel_processor *proc;
	struct rproc *rproc;
	u32 core_id;
	bool unused;
};

/**
 * struct snps_accel_rproc_priv - Private data of remote processor.
 * @proc: Processor to which a remote processor refers.
 * @fw_type: Type of firmware of a remote processor.
 * @core: Core if remote processor is specified for each core, otherwise NULL.
 * @rw_state: Read-write state in sysfs.
 */
struct snps_accel_rproc_priv {
	struct snps_accel_processor *proc;
	enum snps_accel_fw_type fw_type;
	struct snps_accel_core *core;
	bool rw_state;
};

/**
 * struct snps_accel_core_group - Core group.
 * @genpd: Generic PM domain.
 * @proc: Processor to which a core group belongs.
 * @group_id: ID.
 * @unused: Unused or not.
 */
struct snps_accel_core_group {
	struct generic_pm_domain genpd;
	struct snps_accel_processor *proc;
	u32 group_id;
	bool unused;
};

/**
 * struct snps_accel_npx - Private data of an NPX processor.
 * @l2_cores: L2 cores array.
 * @groups: Groups array.
 * @num_l2_cores: Number of L2 cores.
 * @num_l1_cores: Number of L1 cores.
 * @num_groups: Number of L1 core groups.
 * @dmi_pbase: Device (physical) base address of DMI registers.
 * @dmi_size: Size of DMI registers.
 */
struct snps_accel_npx {
	struct snps_accel_core *l2_cores[SNPS_ACCEL_NPX_MAX_L2CORES];
	struct snps_accel_core_group groups[SNPS_ACCEL_NPX_MAX_GROUPS];
	u32 num_l2_cores;
	u32 num_l1_cores;
	u32 num_groups;
	phys_addr_t dmi_pbase;
	resource_size_t dmi_size;
};

/**
 * struct snps_accel_vpx - Private data of a VPX processor.
 * @cluster_block_id: Block ID of VPX cluster power domain.
 * @csm_block_id: Block ID of VPX CSM power domain.
 */
struct snps_accel_vpx {
	u32 cluster_block_id;
	u32 csm_block_id;
};

/**
 * struct snps_accel_processor - A processor.
 * @genpd: Generic PM domain.
 * @data: Match data.
 * @pdev: Platform device.
 * @mgr: Manager of a processor.
 * @system: System of a processor
 * @priv: Private data.
 * @cores: Cores.
 * @rprocs: Remote processors array.
 * @mems: Memory regions.
 * @num_cores: Number of cores.
 * @num_rprocs: Number of remote processors.
 * @num_mems: Number of memory regions.
 * @start_core_id: Start core ID.
 * @processor_id: ID.
 * @safety_level: Level of functional safety.
 * @csm_size_mb: Size of cluster-shared memory in MB.
 * @autosuspend_delay_ms: Delay of doing an autosuspend in millisecond.
 * @save_halt_timeout_ms: Timeout waiting for the core to halt in milliseconds.
 * @bootaddr_64bit: Has a 64bit boot address.
 * @firmware_by_type: Firmware(s) are specified by means of core-type.
 * @forbid_rpm_suspend: Forbid suspend during runtime PM.
 * @forbid_rpm_clock: Forbid disable/enable clock during runtime PM.
 * @forbid_rpm_cfg: Forbid configuration during runtime PM.
 * @forbid_rpm_powerdown: Forbid power-down during runtime PM.
 */
struct snps_accel_processor {
	struct generic_pm_domain genpd;
	const struct snps_accel_dev_data *data;
	struct platform_device *pdev;
	struct snps_accel_mgr *mgr;
	struct snps_accel_system *system;
	void *priv;
	struct snps_accel_core cores[SNPS_ACCEL_MAX_CORES_PP];
	struct rproc *rprocs[SNPS_ACCEL_MAX_CORES_PP];
	struct snps_accel_proc_mem *mems;
	u32 num_cores;
	u32 num_rprocs;
	u32 num_mems;
	u32 start_core_id;
	u32 processor_id;
	u32 safety_level;
	u32 csm_size_mb;
	int autosuspend_delay_ms;
	int save_halt_timeout_ms;
	bool bootaddr_64bit;
	bool firmware_by_type;
	bool forbid_rpm_suspend;
	bool forbid_rpm_clock;
	bool forbid_rpm_cfg;
	bool forbid_rpm_powerdown;
};

/**
 * snps_accel_parse_ranges - Parse ranges relates to a processor.
 * @dev: Target device.
 * @offset: Output offset between target device node between its ancestor(s).
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_parse_ranges(struct device *dev, off_t *offset);

/**
 * snps_accel_mbox_send_message - Send a message.
 * @system: System.
 * @msg: Message.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_mbox_send_message(struct snps_accel_system *system,
				 struct snps_accel_mbox_msg *msg);

/**
 * snps_accel_mbox_abort_message - Abort a sent message.
 * @system: System.
 * @msg: Message.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_mbox_abort_message(struct snps_accel_system *system,
				  struct snps_accel_mbox_msg *msg);

/**
 * snps_accel_mem_init - Initialize memory context.
 * @dev: Device.
 * @mem: Memory context.
 */
void snps_accel_mem_init(struct device *dev,
			 struct snps_accel_mem_ctx *mem);

/**
 * snps_accel_release_import - Release import.
 * @mem: Memory context.
 */
void snps_accel_release_import(struct snps_accel_mem_ctx *mem);

/**
 * snps_accel_dmabuf_create - Create DMA buffer.
 * @mem: Memory context.
 * @size: Size.
 * @dflags: Direction flags.
 *
 * Return: Address of DMA buffer on success, otherwise an appropriate error code.
 */
struct snps_accel_mem_buffer *
snps_accel_dmabuf_create(struct snps_accel_mem_ctx *mem, u64 size,
			 u32 dflags);

/**
 * snps_accel_dmabuf_release - Release DMA buffer.
 * @mbuf: Memory buffer.
 */
void snps_accel_dmabuf_release(struct snps_accel_mem_buffer *mbuf);

/**
 * snps_accel_get_dmabuf_info - Get information of DMA buffer.
 * @mem: Memory region context.
 * @info: Output information of DMA buffer.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_get_dmabuf_info(struct snps_accel_mem_ctx *mem,
			       struct snps_accel_dmabuf_info *info);

/**
 * snps_accel_do_dmabuf_import - Import DMA buffer.
 * @mem: Memory context.
 * @fd: File descriptor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_do_dmabuf_import(struct snps_accel_mem_ctx *mem, int fd);

/**
 * snps_accel_do_dmabuf_detach - Detach DMA buffer.
 * @mem: Memory context.
 * @fd: File descriptor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_do_dmabuf_detach(struct snps_accel_mem_ctx *mem, int fd);

/**
 * snps_accel_cores_rpm_get - Runtime PM get multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @cores_wait: Flags to show if a core needs to wait for completion.
 * @num_cw: Number of flags used for showing if cores need to wait for completion.
 * @rpmflags: Runtime PM flag argument bits.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_cores_rpm_get(struct snps_accel_system *system,
			     struct snps_accel_ioctl_rpm_cmd *data,
			     bool *cores_wait, u32 num_cw, int rpmflags);

/**
 * snps_accel_cores_rpm_wait - Wait for completion of Runtime PM get multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @cores_wait: Flags to show if a core needs to wait for completion.
 * @num_cw: Number of flags used for showing if cores need to wait for completion.
 */
void snps_accel_cores_rpm_wait(struct snps_accel_system *system,
			       struct snps_accel_ioctl_rpm_cmd *data,
			       bool *cores_wait, u32 num_cw);

/**
 * snps_accel_cores_rpm_put - Runtime PM put multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @rpmflags: Runtime PM flag argument bits.
 * @put_ret: Return value of the whole put multiple cores at once.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_cores_rpm_put(struct snps_accel_system *system,
			     struct snps_accel_ioctl_rpm_cmd *data,
			     int rpmflags);

/**
 * snps_accel_arcsync_power_on - Power on callback of a manager (ARCSync).
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_power_on(struct generic_pm_domain *genpd);

/**
 * snps_accel_arcsync_power_off - Power off callback of a manager (ARCSync).
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_power_off(struct generic_pm_domain *genpd);

/**
 * snps_accel_runtime_suspend - Runtime suspend callback of a core.
 * @dev: Device of a core.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_runtime_suspend(struct device *dev);

/**
 * snps_accel_runtime_resume - Runtime resume callback of a core.
 * @dev: Device of a core.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_runtime_resume(struct device *dev);

/**
 * snps_accel_npx_pd_probe - Probe a NPX processor's PM domains.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_npx_pd_probe(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_pd_remove - Remove an NPX processor's PM domains.
 * @proc: Target processor.
 */
void snps_accel_npx_pd_remove(struct snps_accel_processor *proc);

/**
 * snps_accel_vpx_pd_probe - Probe a VPX processor's PM domain.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_vpx_pd_probe(struct snps_accel_processor *proc);

/**
 * snps_accel_vpx_pd_remove - Remove a VPX processor's PM domains.
 * @proc: Target processor.
 */
void snps_accel_vpx_pd_remove(struct snps_accel_processor *proc);

/**
 * snps_accel_clear_core_rpm_latency - Clean latency of a given core and its parents power domains.
 * @core: Target core.
 */
void snps_accel_clear_core_rpm_latency(struct snps_accel_core *core);

/**
 * snps_accel_ro_rproc_state_create - Create a read-only state attribute.
 * @rproc: Target remote procesor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_ro_rproc_state_create(struct rproc *rproc);

/**
 * snps_accel_ro_rproc_state_remove - Remove a read-only state attribute.
 * @rproc: Target remote procesor.
 */
void snps_accel_ro_rproc_state_remove(struct rproc *rproc);

/**
 * snps_accel_proc_sysfs_create - Create sysfs attributes of a processor.
 * @proc: Target procesor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_proc_sysfs_create(struct snps_accel_processor *proc);

/**
 * snps_accel_proc_sysfs_remove - Remove sysfs attribute of a processor.
 * @proc: Target procesor.
 */
void snps_accel_proc_sysfs_remove(struct snps_accel_processor *proc);

/**
 * snps_accel_get_as_coreid - Get ARCSync core ID.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @coreid_bw: Bit-width of target core ID in an ARCSync core ID
 *
 * Return: ARCSync core ID.
 */
static inline u32
snps_accel_get_as_coreid(u32 processor_id, u32 core_id, u32 coreid_bw)
{
	return (processor_id << coreid_bw) | core_id;
}

/**
 * snps_accel_as_coreid_to_processor_id - Get processor ID from ARCSync core ID.
 * @as_coreid: Target ARCSync core ID.
 * @coreid_bw: Bit-width of target core ID in an ARCSync core ID
 *
 * Return: Target processor ID.
 */
static inline u32
snps_accel_as_coreid_to_processor_id(u32 as_coreid, u32 coreid_bw)
{
	return as_coreid >> coreid_bw;
}

/**
 * snps_accel_as_coreid_to_core_id - Get core ID from ARCSync core ID.
 * @as_coreid: Target ARCSync core ID.
 * @coreid_bw: Bit-width of target core ID in an ARCSync core ID
 *
 * Return: Target core ID.
 */
static inline u32
snps_accel_as_coreid_to_core_id(u32 as_coreid, u32 coreid_bw)
{
	return as_coreid & ((1U << coreid_bw) - 1);
}

/**
 * snps_accel_arcsync_send_irq - Send an interrupt to target core.
 * @mgr: Manager.
 * @sender_as_coreid: Sender ARCSync core ID.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @index: Manager interrupt index.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_send_irq(struct snps_accel_mgr *mgr,
				u32 sender_as_coreid, u32 processor_id,
				u32 core_id, u32 index);

/**
 * snps_accel_arcsync_abort_irq - Abort an interrupt to target core which was not acknowledged.
 * @mgr: Manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @index: Manager interrupt index.
 */
void snps_accel_arcsync_abort_irq(struct snps_accel_mgr *mgr,
				  u32 processor_id, u32 core_id, u32 index);

/**
 * snps_accel_arcsync_probe - Probe a manager.
 * @pdev: Platform device.
 * @data: Match data.
 *
 * Return: 0 on success and an appropriate error code otherwise.
 */
int snps_accel_arcsync_probe(struct platform_device *pdev, const void *data);

/**
 * snps_accel_arcsync_remove - Remove a manager.
 * @pdev: Platform device.
 */
void snps_accel_arcsync_remove(struct platform_device *pdev);

/**
 * snps_accel_verify_lineage - Verify lineage.
 * @pdev: Target platform device.
 * @p_name: Compatible string of parent device node.
 * @gp_name: Compatible string of grandparent device node.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_verify_lineage(struct platform_device *pdev,
			      const char *p_name,
			      const char *gp_name);

/**
 * snps_accel_system_release - Release a system.
 * @kref: Reference counter of a system.
 */
void snps_accel_system_release(struct kref *kref);

/**
 * to_snps_accel_ctrl_priv - Convert to private data of control device file.
 * @ctx: Memory region context.
 */
static inline struct snps_accel_ctrl_priv *
to_snps_accel_ctrl_priv(struct snps_accel_mem_ctx *ctx)
{
	return container_of(ctx, struct snps_accel_ctrl_priv, mem);
}

/**
 * snps_accel_ctrl_priv_get - Get private data of control device file.
 * @priv: Private data of control device file.
 */
void snps_accel_ctrl_priv_get(struct snps_accel_ctrl_priv *priv);

/**
 * snps_accel_ctrl_priv_put - Put private data of control device file.
 * @priv: Private data of control device file.
 */
void snps_accel_ctrl_priv_put(struct snps_accel_ctrl_priv *priv);

/**
 * snps_accel_get_device - Get device struct by compatible and id.
 * @compatible: Compatible string of a device node.
 * @id_propname: Name of ID property.
 * @id: Value of ID property.
 *
 * Return: Device struct on success, otherwise an appropriate failure.
 */
struct device *snps_accel_get_device(const char *compatible,
				     const char *id_propname, u32 id);

/**
 * snps_accel_put_device - Put device.
 * @dev: Device struct.
 *
 * Return: Device struct on success, otherwise an appropriate failure.
 */
void snps_accel_put_device(struct device *dev);

/**
 * snps_accel_npx - Check if a processor is an NPX processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX processor, otherwise %false.
 */
bool snps_accel_npx(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_v1 - Check if a processor is an NPX v1 processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v1 processor, otherwise %false.
 */
bool snps_accel_npx_v1(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_v2p0 - Check if a processor is an NPX v2.0 processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v2.0 processor, otherwise %false.
 */
bool snps_accel_npx_v2p0(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_v2 - Check if a processor is an NPX v2.* processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v2.* processor, otherwise %false.
 */
bool snps_accel_npx_v2(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_l2_core - Check if a core is an NPX L2 core.
 * @core: Target core.
 *
 * Return: %true if a @core is an NPX L2 core, otherwise %false.
 */
bool snps_accel_npx_l2_core(struct snps_accel_core *core);

/**
 * snps_accel_npx_first_l2_core_id - Check if a core ID represents the first NPX L2 core.
 * @proc: Target processor having the target core.
 * @core_id: Target core ID.
 *
 * Return: %true if a @core_id represents the first NPX L2 core of a @proc, otherwise %false.
 */
bool snps_accel_npx_first_l2_core_id(struct snps_accel_processor *proc,
				     u32 core_id);

/**
 * snps_accel_valid_core_id - Check if a core ID is valid.
 * @proc: Target processor having the target core.
 * @core_id: Target core ID.
 *
 * Return: %true if a @core_id is valid for a @proc, otherwise %false.
 */
bool snps_accel_valid_core_id(struct snps_accel_processor *proc, u32 core_id);

/**
 * snps_accel_get_npx_dmi_cfg_vaddr - Get virtual address of NPX DMI CFG.
 * @proc: Target processor.
 *
 * Return: Virtual address of NPX DMI CFG on success, or NULL on failure.
 */
void __iomem *
snps_accel_get_npx_dmi_cfg_vaddr(struct snps_accel_processor *proc);

/**
 * snps_accel_get_mem_paddr - Get physical address of a memory region.
 * @proc: Target processor.
 * @type: Type of a memory region.
 * @paddr: Output physical address.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_get_mem_paddr(struct snps_accel_processor *proc,
			     enum snps_accel_proc_mem_type type,
			     phys_addr_t *paddr);

/**
 * snps_accel_get_mem_daddr - Get device address of a memory region.
 * @proc: Target processor.
 * @type: Type of a memory region.
 * @daddr: Output device address.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_get_mem_daddr(struct snps_accel_processor *proc,
			     enum snps_accel_proc_mem_type type,
			     u64 *daddr);

/**
 * snps_accel_get_mem_size - Get size of a memory region.
 * @proc: Target processor.
 * @type: Type of a memory region.
 * @size: Output size.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_get_mem_size(struct snps_accel_processor *proc,
			    enum snps_accel_proc_mem_type type,
			    resource_size_t *size);

/**
 * snps_accel_proc_rpm_get - Runtime PM get cores of a processor at once.
 * @proc: Target processor.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_proc_rpm_get(struct snps_accel_processor *proc, int rpmflags);

/**
 * snps_accel_proc_rpm_put - Runtime PM put cores of a processor at once.
 * @proc: Target processor.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_proc_rpm_put(struct snps_accel_processor *proc, int rpmflags);

/**
 * snps_accel_proc_boot - Boot a processor.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_proc_boot(struct snps_accel_processor *proc);

/**
 * snps_accel_proc_shutdown - Shutdown a processor.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_proc_shutdown(struct snps_accel_processor *proc);

/**
 * snps_accel_npx_probe - Probe an NPX processor.
 * @pdev: Platform device of the NPX processor.
 * @data: Match data of the Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_npx_probe(struct platform_device *pdev, const void *data);

/**
 * snps_accel_npx_remove - Remove an NPX processor.
 * @pdev: Platform device of the NPX processor.
 */
void snps_accel_npx_remove(struct platform_device *pdev);

/**
 * snps_accel_npx_probe - Probe a VPX processor.
 * @pdev: Platform device of the VPX processor.
 * @data: Match data of the Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_vpx_probe(struct platform_device *pdev, const void *data);

/**
 * snps_accel_vpx_remove - Remove a VPX processor.
 * @pdev: Platform device of the VPX processor.
 */
void snps_accel_vpx_remove(struct platform_device *pdev);

/**
 * snps_accel_npx_cfg_core_group - Configure NPX group.
 * @group: Target NPX group.
 */
void snps_accel_npx_cfg_core_group(struct snps_accel_core_group *group);

/**
 * snps_accel_npx_cfg_l2_core_group - Configure NPX L2 core group.
 * @proc: Target NPX processor.
 */
void snps_accel_npx_cfg_l2_core_group(struct snps_accel_processor *proc);

#endif /* __SNPS_ACCEL_DRV_H__ */
