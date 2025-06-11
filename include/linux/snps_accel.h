/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __LINUX_SNPS_ACCEL_H__
#define __LINUX_SNPS_ACCEL_H__

#include <linux/device.h>
#include <linux/interrupt.h>

/**
 * typedef snps_accel_mgr_irq_handler - Type of accelerator manager's IRQ handler.
 * @index: Interrupt index starting from 0.
 * @irq_id: Interrupt vector ID.
 * @processor_id: IRQ sender's processor ID.
 * @core_id: IRQ sender's core ID.
 * @data: Private data.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
typedef irqreturn_t (*snps_accel_mgr_irq_handler)(u32 index, int irq_id,
						  u32 processor_id,
						  u32 core_id, void *data);

/**
 * enum snps_accel_mgr_type - Power mode.
 * @SNPS_ACCEL_PMODE_UP: Power mode 0.
 * @SNPS_ACCEL_PMODE_DOWN: Power mode 1.
 */
enum snps_accel_pmode {
	SNPS_ACCEL_PMODE_UP = 0,
	SNPS_ACCEL_PMODE_DOWN,
	SNPS_ACCEL_PMODE_NUM,
};

/**
 * enum snps_accel_mgr_type - Type of accelerator manager.
 * @SNPS_ACCEL_MGR_TYPE_ARCSYNC: ARCSync.
 */
enum snps_accel_mgr_type {
	SNPS_ACCEL_MGR_TYPE_ARCSYNC = 0,
	SNPS_ACCEL_MGR_TYPE_NUM,
};

/**
 * enum snps_accel_mgr_type - Count type of accelerator manager PMU.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_RST: Reset.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_PU: Power up.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_PD: Power down.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG1: Core logic 1.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG2: core logic 2.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG1: Group logic 1.
 * @SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG2: Group logic 2.
 */
enum snps_accel_mgr_pmu_cnt_type {
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_RST = 0,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_PU,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_PD,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG1,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG2,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG1,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG2,
	SNPS_ACCEL_MGR_PMU_CNT_TYPE_NUM,
};

/**
 * struct snps_accel_mgr_ops - Accelerator manager operations.
 * @get_mgr_id: Get manager's ID.
 * @get_type: Get manager's type.
 * @get_version: Get manager's version.
 * @get_num_processors: Get the number of processors managed by the manager.
 * @get_max_cores_per_processor: Get the maximum number of cores per processor.
 * @get_has_pmu: Get if the manager is configured with PMU.
 * @get_num_cores_of_processor: Get the number of cores of a processor.
 * @set_core_boot_addr: Set boot address for a core.
 * @get_core_boot_addr: Get boot address for a core.
 * @set_core_clock: Set the clock of a core.
 * @get_core_clock: Get the clock status of a core.
 * @set_core_group_clock: Set the clock of a core group.
 * @get_core_group_clock: Get the clock status of a core group.
 * @set_core_reset: Set the reset of a core.
 * @reset_core: Reset a core (assert-deassert cycling).
 * @get_core_reset_status: Get the reset status of a core.
 * @set_core_group_reset: Set the reset of a core group.
 * @get_core_group_reset_status: Get the reset of a core group.
 * @set_l2_core_group_reset: Set the reset of L2 core group.
 * @get_l2_core_group_reset_status: Get the reset of L2 core group.
 * @halt_core: Halt a core.
 * @run_core: Run a core.
 * @get_core_status: Get status of a core.
 * @set_core_power_mode: Set power mode of a core.
 * @set_core_group_power_mode: Set power mode of a core group.
 * @set_pmu_count: Set a kind of count of PMU.
 * @get_pmu_count: Get a kind of count of PMU.
 * @register_irq_handler: Register a handler for one of manager's IRQ(s).
 * @deregister_irq_handler: Deregister a handler for one of manager's IRQ(s).
 */
struct snps_accel_mgr_ops {
	/* Manager configuration */
	int (*get_mgr_id)(void *handle, u32 *result);
	enum snps_accel_mgr_type (*get_type)(void);
	int (*get_version)(void *handle, u32 *result);
	int (*get_num_processors)(void *handle, u32 *result);
	int (*get_max_cores_per_processor)(void *handle, u32 *result);
	int (*get_has_pmu)(void *handle, bool *result);
	int (*get_num_cores_of_processor)(void *handle, u32 processor_id,
					  u32 *result);

	/* Boot address control */
	int (*set_core_boot_addr)(void *handle, u32 processor_id,
				  u32 core_id, bool boot_addr_64bit,
				  u64 boot_addr);
	int (*get_core_boot_addr)(void *handle, u32 processor_id,
				  u32 core_id, bool boot_addr_64bit,
				  u64 *boot_addr);

	/* Clock control & status */
	int (*set_core_clock)(void *handle, u32 processor_id,
			      u32 core_id, bool enable);
	int (*get_core_clock)(void *handle, u32 processor_id,
			      u32 core_id, bool *enable);
	int (*set_core_group_clock)(void *handle, u32 processor_id,
				    u32 group_id, bool enable);
	int (*get_core_group_clock)(void *handle, u32 processor_id,
				    u32 group_id, bool *enable);
	int (*set_vpx_block_clock)(void *handle, u32 vpx_block_id,
				   bool enable);
	int (*get_vpx_block_clock)(void *handle, u32 vpx_block_id,
				   bool *enable);

	/* Reset control & status */
	int (*set_core_reset)(void *handle, u32 processor_id,
			      u32 core_id, bool assert);
	int (*reset_core)(void *handle, u32 processor_id, u32 core_id);
	int (*get_core_reset_status)(void *handle, u32 processor_id,
				     u32 core_id, u32 *status);
	int (*set_core_group_reset)(void *handle, u32 processor_id,
				    u32 group_id, bool assert);
	int (*get_core_group_reset_status)(void *handle, u32 processor_id,
					   u32 group_id, u32 *status);
	int (*set_l2_core_group_reset)(void *handle, u32 processor_id,
				       bool assert);
	int (*get_l2_core_group_reset_status)(void *handle, u32 processor_id,
					      u32 *status);
	int (*set_vpx_block_reset)(void *handle, u32 vpx_block_id,
				   bool assert);
	int (*get_vpx_block_reset_status)(void *handle, u32 vpx_block_id,
					  u32 *status);

	/* Halt/Run control & status */
	int (*halt_core)(void *handle, u32 processor_id, u32 core_id,
			 bool check_finish);
	int (*run_core)(void *handle, u32 processor_id, u32 core_id,
			bool check_finish);
	int (*get_core_status)(void *handle, u32 processor_id, u32 core_id,
			       u32 *status);

	/* Power-domain(s) management unit */
	int (*set_core_power_mode)(void *handle, u32 processor_id,
				   u32 core_id, enum snps_accel_pmode pmode,
				   bool check_finish);
	int (*set_core_group_power_mode)(void *handle, u32 processor_id,
					 u32 group_id,
					 enum snps_accel_pmode pmode,
					 bool check_finish);
	int (*set_vpx_block_power_mode)(void *handle, u32 vpx_block_id,
					enum snps_accel_pmode pmode,
					bool check_finish);
	int (*set_pmu_count)(void *handle, u32 processor_id,
			     enum snps_accel_mgr_pmu_cnt_type type, u32 count);
	int (*get_pmu_count)(void *handle, u32 processor_id,
			     enum snps_accel_mgr_pmu_cnt_type type,
			     u32 *count);

	/* Interrupt */
	int (*register_irq_handler)(void *handle,
				    snps_accel_mgr_irq_handler handler,
				    void *data, u32 index);
	int (*deregister_irq_handler)(void *handle,
				      snps_accel_mgr_irq_handler handler,
				      u32 index);
};

/**
 * struct snps_accel_custom_ops - Customer-specific operations
 * @set_core_power_mode: Customer-specific operation to set power mode of a core.
 *			 Once the operation is specified, it will override the default
 *			 one provided by accelerator manager's operation.
 * @set_core_group_power_mode: Customer-specific operation to set power mode of a core group.
 *			       Once the operation is specified, it will override the default
 *			       one provided by accelerator manager's operation.
 * @set_vpx_block_power_mode: Customer-specific operation to set power mode of a VPX block.
 *			      Once the operation is specified, it will override the default
 *			      one provided by accelerator manager's operation.
 * @set_mgr_power_mode: Customer-specific operation to set power mode of a accelerator manager.
 *			Accelerator manager(s) are always-on by default.
 */
struct snps_accel_custom_ops {
	void (*set_core_power_mode)(u32 processor_id, u32 core_id,
				    enum snps_accel_pmode pmode,
				    void *custom_ops_data);
	void (*set_core_group_power_mode)(u32 processor_id, u32 group_id,
					  enum snps_accel_pmode pmode,
					  void *custom_ops_data);
	void (*set_vpx_block_power_mode)(u32 vpx_block_id,
					 enum snps_accel_pmode pmode,
					 void *custom_ops_data);
	void (*set_mgr_power_mode)(u32 mgr_id, enum snps_accel_pmode pmode,
				   void *custom_ops_data);
};

/**
 * snps_accel_get_mgr_handle - Get handle of a manager.
 * @compatible: Compatible string defines a manager in DTS (e.g., "snps,accel-arcsync").
 * @mgr_id: Manager ID.
 *
 * Users must put the got handle after the use.
 *
 * Return: Handle of a manager on success, otherwise an appropriate failure.
 */
extern void *snps_accel_get_mgr_handle(const char *compatible,
				       u32 mgr_id);

/**
 * snps_accel_put_mgr_handle - Put handle of a manager.
 * @handle: Handle of a manager.
 */
extern void snps_accel_put_mgr_handle(void *handle);

/**
 * snps_accel_get_mgr_ops - Get operations of a manager.
 * @handle: Handle of a manager.
 *
 * Return: Operations of a manager on success, otherwise an appropriate failure.
 */
extern const struct snps_accel_mgr_ops *snps_accel_get_mgr_ops(void *handle);

/**
 * snps_accel_mgr_custom_ops_register - Register custom operations to a manager.
 * @handle: Handle of a manager.
 * @custom_ops: Custom operations.
 * @custom_ops_data: Private data as an argument of custom operations.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
extern int snps_accel_mgr_custom_ops_register(void *handle,
				struct snps_accel_custom_ops *custom_ops,
				void *custom_ops_data);

/**
 * snps_accel_core_halted - Check if a core is halted.
 * @core_status: Status of a core.
 *
 * Return: true if halted, otherwise false.
 */
static inline bool snps_accel_core_halted(u32 core_status)
{
	return ((core_status & 1) != 0) ? true : false;
}

/**
 * snps_accel_core_tf_halted - Check if a core is halted due to a triple fault exception.
 * @core_status: Status of a core.
 *
 * Return: true if halted due to a triple fault exception, otherwise false.
 */
static inline bool snps_accel_core_tf_halted(u32 core_status)
{
	return ((core_status >> 1 & 1) != 0) ? true : false;
}

/**
 * snps_accel_core_sleeping - Check if a core is sleeping.
 * @core_status: Status of a core.
 *
 * Return: true if sleeping, otherwise false.
 */
static inline bool snps_accel_core_sleeping(u32 core_status)
{
	return ((core_status >> 2) & 1) ? true : false;
}

/**
 * snps_accel_core_running - Check if a core is running.
 * @core_status: Status of a core.
 *
 * Return: true if running, otherwise false.
 */
static inline bool snps_accel_core_running(u32 core_status)
{
	return ((core_status & 7) == 0) ? true : false;
}

#endif /* __LINUX_SNPS_ACCEL_H__ */
