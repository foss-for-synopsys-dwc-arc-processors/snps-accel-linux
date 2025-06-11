// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/version.h>
#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/of_address.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>

#include "snps_accel_drv.h"

#include "../../remoteproc/remoteproc_internal.h"
#include "../../remoteproc/remoteproc_elf_helpers.h"

/**
 * snps_accel_vpx - Check if a processor is a VPX processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX processor, otherwise %false.
 */
bool snps_accel_vpx(struct snps_accel_processor *proc)
{
	if (proc->data->type >= SNPS_ACCEL_DEV_TYPE_VPX5 &&
	    proc->data->type < SNPS_ACCEL_DEV_TYPE_VPX_END)
		return true;

	return false;
}

/**
 * snps_accel_npx - Check if a processor is an NPX processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX processor, otherwise %false.
 */
bool snps_accel_npx(struct snps_accel_processor *proc)
{
	if (proc->data->type >= SNPS_ACCEL_DEV_TYPE_NPX6_V1 &&
	    proc->data->type < SNPS_ACCEL_DEV_TYPE_NPX_END)
		return true;

	return false;
}

/**
 * snps_accel_npx_v1 - Check if a processor is an NPX v1 processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v1 processor, otherwise %false.
 */
bool snps_accel_npx_v1(struct snps_accel_processor *proc)
{
	if (proc->data->type == SNPS_ACCEL_DEV_TYPE_NPX6_V1)
		return true;

	return false;
}

/**
 * snps_accel_npx_v2p0 - Check if a processor is an NPX v2.0 processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v2.0 processor, otherwise %false.
 */
bool snps_accel_npx_v2p0(struct snps_accel_processor *proc)
{
	if (proc->data->type == SNPS_ACCEL_DEV_TYPE_NPX6_V2)
		return true;

	return false;
}

/**
 * snps_accel_npx_v2 - Check if a processor is an NPX v2.* processor.
 * @proc: Target processor.
 *
 * Return: %true if a @proc is an NPX v2.* processor, otherwise %false.
 */
bool snps_accel_npx_v2(struct snps_accel_processor *proc)
{
	if (proc->data->type >= SNPS_ACCEL_DEV_TYPE_NPX6_V2 &&
	    proc->data->type < SNPS_ACCEL_DEV_TYPE_NPX_END)
		return true;

	return false;
}

/**
 * snps_accel_npx_l2_core - Check if a core is an NPX L2 core.
 * @core: Target core.
 *
 * Return: %true if a @core is an NPX L2 core, otherwise %false.
 */
bool snps_accel_npx_l2_core(struct snps_accel_core *core)
{
	struct snps_accel_processor *proc = core->proc;

	if (snps_accel_npx_v1(proc))
		return core->core_id == 0 ? true : false;

	if (snps_accel_npx_v2(proc)) {
		if (proc->num_cores < 10)
			return core->core_id == 0 ? true : false;

		if (core->core_id == 0 ||
		    (core->core_id == (proc->num_cores - 1)))
			return true;
	}

	return false;
}

/**
 * snps_accel_npx_first_l2_core_id - Check if a core ID represents the first NPX L2 core.
 * @proc: Target processor having the target core.
 * @core_id: Target core ID.
 *
 * Return: %true if a @core_id represents the first NPX L2 core of a @proc, otherwise %false.
 */
bool snps_accel_npx_first_l2_core_id(struct snps_accel_processor *proc,
				     u32 core_id)
{
	if (snps_accel_npx(proc))
		return core_id == 0 ? true : false;

	return false;
}

/**
 * snps_accel_valid_core_id - Check if a core ID is valid.
 * @proc: Target processor having the target core.
 * @core_id: Target core ID.
 *
 * Return: %true if a @core_id is valid for a @proc, otherwise %false.
 */
bool snps_accel_valid_core_id(struct snps_accel_processor *proc, u32 core_id)
{
	if (!snps_accel_npx(proc))
		return core_id < proc->num_cores;

	if (proc->num_cores == 1)
		return core_id == 1;

	return core_id < proc->num_cores;
}

/**
 * snps_accel_get_npx_dmi_cfg_vaddr - Get virtual address of NPX DMI CFG.
 * @proc: Target processor.
 *
 * Return: Virtual address of NPX DMI CFG on success, or NULL on failure.
 */
void __iomem *
snps_accel_get_npx_dmi_cfg_vaddr(struct snps_accel_processor *proc)
{
	int i;

	for (i = 0; i < proc->num_mems; i++) {
		if (proc->mems[i].type ==
		    SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG)
			return proc->mems[i].virt_addr.iomem;
	}

	return NULL;
}

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
			     phys_addr_t *paddr)
{
	int i;
	bool found = false;

	if (!paddr)
		return -EINVAL;

	for (i = 0; i < proc->num_mems; i++) {
		if (proc->mems[i].type == type) {
			found = true;
			*paddr = proc->mems[i].phys_addr;
			break;
		}
	}

	return found ? 0 : -ENOENT;
}

/**
 * snps_accel_get_mem_daddr - Get device address of a memory region.
 * @proc: Target processor.
 * @type: Type of a memory region.
 * @daddr: Output device address.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_get_mem_daddr(struct snps_accel_processor *proc,
			     enum snps_accel_proc_mem_type type, u64 *daddr)
{
	int i;
	bool found = false;

	if (!daddr)
		return -EINVAL;

	for (i = 0; i < proc->num_mems; i++) {
		if (proc->mems[i].type == type) {
			found = true;
			*daddr = proc->mems[i].dev_addr;
			break;
		}
	}

	return found ? 0 : -ENOENT;
}

/**
 * snps_accel_get_mem_daddr_and_size - Get device address and size of a memory region.
 * @proc: Target processor.
 * @type: Type of a memory region.
 * @daddr: Output device address.
 * @size: Output size of a memory region.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_get_mem_daddr_and_size(struct snps_accel_processor *proc,
				      enum snps_accel_proc_mem_type type,
				      u64 *daddr, resource_size_t *size)
{
	int i;
	bool found = false;

	if (!daddr || !size)
		return -EINVAL;

	for (i = 0; i < proc->num_mems; i++) {
		if (proc->mems[i].type == type) {
			found = true;
			*daddr = proc->mems[i].dev_addr;
			*size = proc->mems[i].size;
			break;
		}
	}

	return found ? 0 : -ENOENT;
}

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
			    resource_size_t *size)
{
	int i;
	bool found = false;

	if (!size)
		return -EINVAL;

	for (i = 0; i < proc->num_mems; i++) {
		if (proc->mems[i].type == type) {
			found = true;
			*size = proc->mems[i].size;
			break;
		}
	}

	return found ? 0 : -ENOENT;
}

/**
 * __snps_accel_proc_free - Free a processor instance.
 * @proc: Target processor.
 */
static void __snps_accel_proc_free(struct snps_accel_processor *proc)
{
	kfree(proc);
}

/**
 * __snps_accel_proc_alloc - Allocate a processor instance.
 * @pdev: Platform device of target processor.
 * @data: Match data of target processor.
 * @len: Length of private data struct.
 *
 * Return: Memory address of a processor instance, or NULL on failure.
 */
static struct snps_accel_processor *
__snps_accel_proc_alloc(struct platform_device *pdev, const void *data,
			int len)
{
	struct snps_accel_processor *proc;

	if (!pdev)
		return NULL;

	proc = kzalloc(sizeof(struct snps_accel_processor) + len, GFP_KERNEL);
	if (!proc)
		return NULL;

	proc->pdev = pdev;
	proc->data = data;
	proc->priv = &proc[1];

	return proc;
}

/**
 * snps_accel_proc_free - Devm wrapper of freeing a processor instance.
 * @dev: Device of target processor.
 * @res: Target processor instance.
 */
static void snps_accel_proc_free(struct device *dev, void *res)
{
	__snps_accel_proc_free(*(struct snps_accel_processor **)res);
}

/**
 * snps_accel_devm_proc_alloc - Devm wrapper of allocating a processor instance.
 * @pdev: Platform device of target processor.
 * @data: Match data of target processor.
 * @len: Length of private data struct.
 *
 * Return: Memory address of a processor instance, or NULL on failure.
 */
static struct snps_accel_processor *
snps_accel_devm_proc_alloc(struct platform_device *pdev, const void *data,
			   int len)
{
	struct snps_accel_processor **ptr, *proc;

	ptr = devres_alloc(snps_accel_proc_free, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	proc = __snps_accel_proc_alloc(pdev, data, len);
	if (proc) {
		*ptr = proc;
		devres_add(&pdev->dev, ptr);
	} else
		devres_free(ptr);

	return proc;
}

/**
 * snps_accel_proc_rpm_get - Runtime PM get cores of a processor at once.
 * @proc: Target processor.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_proc_rpm_get(struct snps_accel_processor *proc, int rpmflags)
{
	struct snps_accel_system *system = proc->system;
	struct snps_accel_core *core;
	struct snps_accel_ioctl_rpm_cmd data;
	int i, ret;
	bool cores_async[SNPS_ACCEL_IOCTL_MAX_RPM_CORES] = {false};

	data.num_cores = 0;
	data.core_id_bw = 16;
	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (core->unused)
			continue;

		data.cores[data.num_cores].id =
			(proc->processor_id << data.core_id_bw) | core->core_id;
		data.cores[data.num_cores].result = 0;
		data.num_cores++;
	}

	ret = snps_accel_cores_rpm_get(system, &data, cores_async,
				       SNPS_ACCEL_IOCTL_MAX_RPM_CORES,
				       rpmflags);
	if (ret)
		return ret;

	if ((rpmflags & RPM_ASYNC) == 0)
		snps_accel_cores_rpm_wait(system, &data, cores_async,
					  SNPS_ACCEL_IOCTL_MAX_RPM_CORES);

	return ret;
}

/**
 * snps_accel_proc_rpm_put - Runtime PM put cores of a processor at once.
 * @proc: Target processor.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_proc_rpm_put(struct snps_accel_processor *proc, int rpmflags)
{
	struct snps_accel_system *system = proc->system;
	struct snps_accel_core *core;
	struct snps_accel_ioctl_rpm_cmd data;
	int i;

	data.num_cores = 0;
	data.core_id_bw = 16;
	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (core->unused)
			continue;

		data.cores[data.num_cores].id =
			(proc->processor_id << data.core_id_bw) | core->core_id;
		data.cores[data.num_cores].result = 0;
		data.num_cores++;
	}

	return snps_accel_cores_rpm_put(system, &data, rpmflags);
}

/**
 * snps_accel_proc_boot - Boot a processor.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_proc_boot(struct snps_accel_processor *proc)
{
	struct snps_accel_rproc_priv *priv;
	int ret, i;
	bool done[SNPS_ACCEL_MAX_CORES_PP] = {false};

	for (i = 0; i < proc->num_rprocs; i++) {
		if (proc->rprocs[i]->state == RPROC_RUNNING ||
		    proc->rprocs[i]->state == RPROC_ATTACHED) {
			return -EBUSY;
		}
	}

	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_L2_CORE) {
			ret = rproc_boot(proc->rprocs[i]);
			if (ret)
				goto out_l2;

			done[i] = true;
		}
	}

	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_CORE && !done[i]) {
			ret = rproc_boot(proc->rprocs[i]);
			if (ret)
				goto out_l1;

			done[i] = true;
		}
	}

	return 0;

out_l1:
	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_CORE && done[i])
			rproc_shutdown(proc->rprocs[i]);
	}

out_l2:
	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_L2_CORE && done[i])
			rproc_shutdown(proc->rprocs[i]);
	}

	return ret;
}

/**
 * snps_accel_proc_shutdown - Shutdown a processor.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
int snps_accel_proc_shutdown(struct snps_accel_processor *proc)
{
	struct snps_accel_rproc_priv *priv;
	int i;

	for (i = 0; i < proc->num_rprocs; i++) {
		if (proc->rprocs[i]->state != RPROC_RUNNING &&
		    proc->rprocs[i]->state != RPROC_ATTACHED) {
			continue;
		}

		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_CORE)
			rproc_shutdown(proc->rprocs[i]);
	}

	for (i = 0; i < proc->num_rprocs; i++) {
		if (proc->rprocs[i]->state != RPROC_RUNNING &&
		    proc->rprocs[i]->state != RPROC_ATTACHED) {
			continue;
		}

		priv = proc->rprocs[i]->priv;
		if (priv->fw_type == SNPS_ACCEL_FW_TYPE_L2_CORE)
			rproc_shutdown(proc->rprocs[i]);
	}

	return 0;
}

static int
snps_accel_npx_deassert_core_groups(struct snps_accel_processor *proc)
{
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_core *core;
	int i, ret;

	/* Deassert reset of L2 core group */
	ret = mgr->ops->set_l2_core_group_reset(mgr->dev, proc->processor_id,
						false);
	if (ret)
		return ret;

	/* Deassert reset of L2 core(s) */
	for (i = 0; i < npx->num_l2_cores; i++) {
		core = npx->l2_cores[i];
		if (core->unused)
			continue;

		ret = mgr->ops->set_core_reset(mgr->dev, proc->processor_id,
					       core->core_id, false);
		if (ret)
			return ret;
	}

	/* Deassert reset of core group(s) */
	for (i = 0; i < npx->num_groups; i++) {
		ret = mgr->ops->set_core_group_reset(mgr->dev,
						     proc->processor_id,
						     i, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int snps_accel_rproc_prepare(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_core *core;
	int i, ret;
	u32 version;

	if (mgr->ops->get_type() != SNPS_ACCEL_MGR_TYPE_ARCSYNC)
		return 0;

	ret = mgr->ops->get_version(mgr->dev, &version);
	if (ret)
		return ret;

	if (version == 1)
		return 0;

	if (snps_accel_npx(proc) && (proc->num_cores > 1) &&
	    (priv->fw_type == SNPS_ACCEL_FW_TYPE_L2_CORE)) {
		ret = snps_accel_npx_deassert_core_groups(proc);
		if (ret)
			return ret;
	}

	if (!proc->firmware_by_type) {
		core = priv->core;
		if (core->unused)
			return -ENODEV;

		mgr->ops->set_core_reset(mgr->dev, proc->processor_id,
					 core->core_id, false);
		return 0;
	}

	for (i = 0; i < proc->num_cores; i++) {
		core = &proc->cores[i];
		if (core->unused)
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		mgr->ops->set_core_reset(mgr->dev, proc->processor_id,
					 core->core_id, false);
	}

	return 0;
}

/**
 * snps_accel_rproc_core_start - Start a remote processor core.
 * @rproc: Target remote processor.
 *
 * The remote processor represents a core which means the firmware
 * is specified by each core ID. In other words, each core has their
 * own firmware.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
static int snps_accel_rproc_core_start(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_core *core = priv->core;
	struct device *dev;
	int ret;

	if (!core)
		return -EINVAL;

	if (core->unused)
		return 0;

	mgr->ops->set_core_boot_addr(mgr->dev, proc->processor_id,
				     core->core_id, proc->bootaddr_64bit,
				     rproc->bootaddr);
	mgr->ops->reset_core(mgr->dev, proc->processor_id, core->core_id);

	if (snps_accel_npx_l2_core(core))
		return 0;

	dev = &core->pdev->dev;
	if (proc->forbid_rpm_suspend)
		pm_runtime_forbid(dev);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		pm_runtime_disable(dev);
		return ret;
	}

	return 0;
}

/**
 * snps_accel_a_qualified_core - Check if it is a qualified core to use a firmware.
 * @core: Target core.
 * @fw_type: Type of firmware.
 *
 * Return: %true if the @core is qualified to use a @fw_type of firmware, otherwise %false.
 */
static bool snps_accel_a_qualified_core(struct snps_accel_core *core,
					enum snps_accel_fw_type fw_type)
{
	if (core->unused)
		return false;

	if ((fw_type == SNPS_ACCEL_FW_TYPE_L2_CORE) &&
	    snps_accel_npx_l2_core(core))
		return true;

	if ((fw_type == SNPS_ACCEL_FW_TYPE_CORE) &&
	    !snps_accel_npx_l2_core(core))
		return true;

	return false;
}

/**
 * snps_accel_rproc_start - Start a remote processor.
 * @rproc: Target remote processor.
 *
 * The remote processor represents a core or a type of core(s).
 * For the former, a firmware is specified by each core ID.
 * For the latter, a firmware is specified by a type of core(s)).
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
static int snps_accel_rproc_start(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_core *core;
	struct device *dev;
	int ret, i, j;

	if (!proc->firmware_by_type)
		return snps_accel_rproc_core_start(rproc);

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		mgr->ops->set_core_boot_addr(mgr->dev, proc->processor_id,
					     core->core_id,
					     proc->bootaddr_64bit,
					     rproc->bootaddr);
		mgr->ops->reset_core(mgr->dev, proc->processor_id,
				     core->core_id);

		if (snps_accel_npx_l2_core(core))
			continue;

		dev = &core->pdev->dev;
		if (proc->forbid_rpm_suspend)
			pm_runtime_forbid(dev);

		pm_runtime_enable(dev);
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		dev = &core->pdev->dev;
		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			goto out_boot;

		snps_accel_clear_core_rpm_latency(core);
	}

	return 0;

out_boot:
	if (i > proc->start_core_id)
		for (j = i - 1; j >= proc->start_core_id; j--) {
			core = &proc->cores[j];
			if (!snps_accel_a_qualified_core(core, priv->fw_type))
				continue;

			if (snps_accel_npx_l2_core(core))
				continue;

			dev = &core->pdev->dev;
			pm_runtime_mark_last_busy(dev);
			pm_runtime_put_autosuspend(dev);
		}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		dev = &core->pdev->dev;
		pm_runtime_disable(dev);
	}

	return ret;
}

/**
 * snps_accel_verify_pm_usage_count - Verify PM usage count.
 * @dev: Device of target core.
 * @correct: Whether to correct invalid usage counts.
 *
 * A regular shutdown must need a correct usage count.
 *
 * Return: %true if the usage count is expected for @dev, otherwise %false.
 */
static bool snps_accel_verify_pm_usage_count(struct device *dev, bool correct)
{
	int usage_count, expected_usage_count;

	expected_usage_count = dev->power.runtime_auto ? 0 : 1;
	if (correct)
		while (correct) {
			usage_count = atomic_read(&dev->power.usage_count);
			if (usage_count > expected_usage_count)
				pm_runtime_put_noidle(dev);
			else
				break;
		}
	else
		usage_count = atomic_read(&dev->power.usage_count);

	if (usage_count != expected_usage_count) {
		dev_err(dev, "Incorrect usage count: %d vs Expected: %d",
			usage_count, expected_usage_count);
		return false;
	}

	return true;
}

/**
 * snps_accel_rproc_core_stop - Stop a remote processor core.
 * @rproc: Target remote processor.
 *
 * The remote processor represents a core which means the firmware
 * is specified by each core ID. In other words, each core has their
 * own firmware.
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
static int snps_accel_rproc_core_stop(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct snps_accel_core *core = priv->core;
	struct device *dev;
	int ret;

	if (!core)
		return -EINVAL;

	if (core->unused)
		return 0;

	if (snps_accel_npx_l2_core(core))
		return 0;

	dev = &core->pdev->dev;

	if (!snps_accel_verify_pm_usage_count(dev, true))
		return -EACCES;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	if (proc->forbid_rpm_suspend)
		pm_runtime_allow(dev);

	pm_runtime_put_sync_suspend(dev);
	pm_runtime_disable(dev);

	return 0;
}

/**
 * snps_accel_rproc_stop - Stop a remote processor.
 * @rproc: Target remote processor.
 *
 * The remote processor represents a core or a type of core(s).
 * For the former, a firmware is specified by each core ID.
 * For the latter, a firmware is specified by a type of core(s)).
 *
 * Return: 0 on success, otherwise an appropriate failure.
 */
static int snps_accel_rproc_stop(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct snps_accel_core *core;
	struct device *dev;
	int ret, i, j;

	if (!proc->firmware_by_type)
		return snps_accel_rproc_core_stop(rproc);

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		if (!snps_accel_verify_pm_usage_count(&core->pdev->dev, true))
			return -EACCES;
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		dev = &core->pdev->dev;
		ret = pm_runtime_resume_and_get(dev);
		if (ret)
			goto out_get;
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (!snps_accel_a_qualified_core(core, priv->fw_type))
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		dev = &core->pdev->dev;
		if (proc->forbid_rpm_suspend)
			pm_runtime_allow(dev);

		pm_runtime_put_sync_suspend(dev);
		pm_runtime_disable(dev);
	}

	return 0;

out_get:
	if (i > 0)
		for (j = i - 1; j >= 0; j--) {
			core = &proc->cores[j];
			if (!snps_accel_a_qualified_core(core, priv->fw_type))
				continue;

			if (snps_accel_npx_l2_core(core))
				continue;

			dev = &core->pdev->dev;
			pm_runtime_mark_last_busy(dev);
			pm_runtime_put_autosuspend(dev);
		}

	return ret;
}

/**
 * snps_accel_rproc_da_to_va() - Translate device address to host virtual address.
 * @rproc: Target remote processor.
 * @da: Device address.
 * @len: Length of the piece of memory.
 * @is_ram: Output to show if it is REGION_INTERSECTS.
 *
 * Return: Translated virtual address in kernel memory space on success,
 *         or NULL on failure.
 */
static void *
snps_accel_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len,
			  bool *is_ram)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	u32 offset;
	int i;

	if (len <= 0)
		return NULL;

	for (i = 0; i < proc->num_mems; i++) {
		if ((proc->mems[i].type ==
		     SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG) ||
		    (proc->mems[i].type ==
		     SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI))
			continue;

		if (!proc->mems[i].virt_addr.mem)
			continue;

		if ((da >= proc->mems[i].dev_addr) &&
		    (da + len) <=
		    (proc->mems[i].dev_addr + proc->mems[i].size)) {
			offset = da - proc->mems[i].dev_addr;
			dev_dbg(&rproc->dev,
				"da %#llx at off %#x of pa %pap (da %#llx)\n",
				da, offset, &proc->mems[i].phys_addr,
				proc->mems[i].dev_addr);
			*is_ram = (proc->mems[i].is_ram == REGION_INTERSECTS);
			return (__force void *)(proc->mems[i].virt_addr.mem + offset);
		}
	}

	return NULL;
}

/**
 * snps_accel_rproc_elf_load_segments - Load memory sections by parsing program headers.
 * @rproc: Target remote processor.
 * @fw: Firmware instance.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_rproc_elf_load_segments(struct rproc *rproc,
					      const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	const void *ehdr, *phdr;
	int i, ret = 0;
	u16 phnum;
	const u8 *elf_data = fw->data;
	u8 class = fw_elf_get_class(fw);
	u32 elf_phdr_get_size = elf_size_of_phdr(class);

	ehdr = elf_data;
	phnum = elf_hdr_get_e_phnum(class, ehdr);
	phdr = elf_data + elf_hdr_get_e_phoff(class, ehdr);

	/* Go through the available ELF segments */
	for (i = 0; i < phnum; i++, phdr += elf_phdr_get_size) {
		u64 da = elf_phdr_get_p_paddr(class, phdr);
		u64 memsz = elf_phdr_get_p_memsz(class, phdr);
		u64 filesz = elf_phdr_get_p_filesz(class, phdr);
		u64 offset = elf_phdr_get_p_offset(class, phdr);
		u32 type = elf_phdr_get_p_type(class, phdr);
		void *ptr;
		bool is_ram = false;

		if ((type != PT_LOAD) || (memsz == 0))
			continue;

		/*
		 * When an ELF file is created with "NOLOAD" sections, these
		 * sections are typically part of a PT_LOAD segment, but their
		 * filesz (size in file) will be 0, while their memsz (size
		 * in memory) will reflect the allocated memory space. This
		 * indicates that the memory region should be allocated but
		 * not initialized from the file.
		 */
		if (filesz == 0)
			continue;

		dev_dbg(dev, "phdr: da %#llx memsz %#llx filesz %#llx\n",
			da, memsz, filesz);

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz %#llx memsz %#llx\n",
				filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev,
				"truncated fw: need %#llx avail %#zx\n",
				offset + filesz, fw->size);
			ret = -EINVAL;
			break;
		}

		if (!rproc_u64_fit_in_size_t(memsz)) {
			dev_err(dev,
				"size (%llx) does not fit in size_t type\n",
				memsz);
			ret = -EOVERFLOW;
			break;
		}

		/* Grab the kernel address for this device address */
		ptr = rproc_da_to_va(rproc, da, memsz, &is_ram);
		if (!ptr) {
			dev_err(dev,
				"bad phdr da %#llx memsz %#llx is_ram %s\n",
				da, memsz, is_ram ? "true" : "false");
			ret = -EINVAL;
			break;
		}

		/* Put the segment where the remote processor expects it */
		if (filesz)
			memcpy(ptr, elf_data + offset, filesz);

		/*
		 * Zero out remaining memory for this segment.
		 *
		 * This isn't strictly required since dma_alloc_coherent already
		 * did this for us. albeit harmless, we may consider removing
		 * this.
		 */
		if (memsz > filesz)
			memset(ptr + filesz, 0, memsz - filesz);

#if defined(CONFIG_ARM64)
		if (is_ram)
			dcache_clean_inval_poc((unsigned long)ptr,
					       (unsigned long)ptr + memsz);
#endif
	}

	return ret;
}

/**
 * snps_accel_rproc_elf_get_boot_addr - Get rproc's boot address.
 * @rproc: The remote processor handle
 * @fw: The ELF firmware image
 *
 * Return: Address of section .vectors.
 */
u64 snps_accel_rproc_elf_get_boot_addr(struct rproc *rproc,
				       const struct firmware *fw)
{
	struct device *dev = &rproc->dev;
	const void *shdr, *name_table_shdr;
	int i;
	const char *name_table;
	const u8 *elf_data = (void *)fw->data;
	u8 class = fw_elf_get_class(fw);
	const void *ehdr = elf_data;
	u16 shnum = elf_hdr_get_e_shnum(class, ehdr);
	u32 elf_shdr_get_size = elf_size_of_shdr(class);
	u16 shstrndx = elf_hdr_get_e_shstrndx(class, ehdr);
	u64 sh_addr;

	/* First, get the section header according to the elf class */
	shdr = elf_data + elf_hdr_get_e_shoff(class, ehdr);

	/* Compute name table section header entry in shdr array */
	name_table_shdr = shdr + (shstrndx * elf_shdr_get_size);

	/* Finally, compute the name table section address in elf */
	name_table = elf_data + elf_shdr_get_sh_offset(class, name_table_shdr);

	for (i = 0; i < shnum; i++, shdr += elf_shdr_get_size) {
		u32 name = elf_shdr_get_sh_name(class, shdr);

		if (!strcmp(name_table + name, ".vectors")) {
			sh_addr = elf_shdr_get_sh_addr(class, shdr);
			dev_info(dev,
				 "Found .vectors section at addr %#llx\n",
				 sh_addr);
			return sh_addr;
		}
	}

	BUG_ON(1);
	return 0;
}

static const struct rproc_ops snps_accel_rproc_ops = {
	.prepare = snps_accel_rproc_prepare,
	.start = snps_accel_rproc_start,
	.stop = snps_accel_rproc_stop,
	.da_to_va = snps_accel_rproc_da_to_va,
	.get_boot_addr = snps_accel_rproc_elf_get_boot_addr,
	.load = snps_accel_rproc_elf_load_segments,
	.sanity_check = rproc_elf_sanity_check,
};

struct snps_accel_fw_by_type {
	const char *fw_prop_name;
	enum snps_accel_fw_type fw_type;
};

static struct snps_accel_fw_by_type snps_accel_fw_by_type[] = {
	{
		.fw_prop_name = "snps,firmware-name-l2",
		.fw_type = SNPS_ACCEL_FW_TYPE_L2_CORE,
	},
	{
		.fw_prop_name = "firmware-name",
		.fw_type = SNPS_ACCEL_FW_TYPE_CORE,
	},
};
static size_t snps_accel_fw_by_type_len = ARRAY_SIZE(snps_accel_fw_by_type);

/**
 * snps_accel_rprocs_probe - Probe remote processor(s) of a processor.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_rprocs_probe(struct snps_accel_processor *proc)
{
	struct platform_device *pdev = proc->pdev;
	struct device *dev = &pdev->dev;
	struct snps_accel_system *system = dev_get_drvdata(dev->parent);
	struct device_node *node = dev->of_node;
	struct snps_accel_rproc_priv *priv;
	struct snps_accel_core *core;
	const char *name_lookup;
	const char *name_result;
	int ret, i, j;
	bool rw_state;

	proc->num_rprocs = 0;
	proc->firmware_by_type =
		(of_get_property(node, "firmware-name", &ret) != NULL) ?
		true : false;
	rw_state = of_property_read_bool(node, "snps,rproc-rw-state");

	if (proc->firmware_by_type) {
		/* Specify firmware by core-type */
		if (snps_accel_fw_by_type_len > SNPS_ACCEL_MAX_CORES_PP) {
			dev_err(dev,
				"Invalid length of firmware by type %zu\n",
				snps_accel_fw_by_type_len);
			return -EINVAL;
		}

		for (i = 0; i < snps_accel_fw_by_type_len; i++) {
			if ((snps_accel_fw_by_type[i].fw_type ==
			     SNPS_ACCEL_FW_TYPE_L2_CORE) &&
			    (proc->num_cores == 1))
				continue;

			if ((snps_accel_fw_by_type[i].fw_type ==
			     SNPS_ACCEL_FW_TYPE_L2_CORE) &&
			    !snps_accel_npx(proc))
				continue;

			ret = of_property_read_string(node,
					snps_accel_fw_by_type[i].fw_prop_name,
					&name_result);
			if (ret) {
				dev_err(dev, "Failed to read '%s'\n",
					snps_accel_fw_by_type[i].fw_prop_name);
				return -EINVAL;
			}

			proc->rprocs[proc->num_rprocs] =
				devm_rproc_alloc(dev, node->name,
					&snps_accel_rproc_ops, name_result,
					sizeof(struct snps_accel_rproc_priv));
			if (!proc->rprocs[proc->num_rprocs])
				return -ENOMEM;

			/* User-mode library controls the remote processor */
			proc->rprocs[proc->num_rprocs]->auto_boot = false;

			priv = proc->rprocs[proc->num_rprocs]->priv;
			priv->rw_state = rw_state;

			/* A user-friendly name instead of 'remoteprocX' */
			dev_set_name(&proc->rprocs[proc->num_rprocs]->dev,
				     "snps_accel_rproc_%u_%u_%s",
				     system->system_id, proc->processor_id,
				     snps_accel_get_fw_type_string(
					snps_accel_fw_by_type[i].fw_type));

			priv->fw_type = snps_accel_fw_by_type[i].fw_type;
			priv->proc = proc;
			priv->core = NULL;

			ret = devm_rproc_add(dev, proc->rprocs[proc->num_rprocs]);
			if (ret) {
				dev_err(dev, "Failed to add rproc #%d: %d\n",
					i, ret);
				return ret;
			}

			for (j = proc->start_core_id;
			     j < proc->start_core_id + proc->num_cores; j++) {
				core = &proc->cores[j];
				if (!snps_accel_a_qualified_core(core,
					snps_accel_fw_by_type[i].fw_type))
					continue;

				core->rproc = proc->rprocs[proc->num_rprocs];
			}

			proc->num_rprocs++;
		}
	} else {
		/* Specify firmware by core-id */
		if (!snps_accel_npx(proc)) {
			dev_err(dev, "Only NPX can be specified by core-id\n");
			return -EINVAL;
		}

		for (i = proc->start_core_id;
		     i < proc->start_core_id + proc->num_cores; i++) {
			core = &proc->cores[i];
			if (core->unused)
				continue;

			name_lookup = kasprintf(GFP_KERNEL,
						"snps,firmware-name-core%u",
						core->core_id);
			ret = of_property_read_string(node, name_lookup,
						      &name_result);
			if (ret) {
				dev_err(dev,
					"Failed to read '%s': %d\n",
					name_lookup, ret);
				kfree(name_lookup);
				return ret;
			}
			kfree(name_lookup);

			proc->rprocs[proc->num_rprocs] =
				devm_rproc_alloc(dev, node->name,
					&snps_accel_rproc_ops, name_result,
					sizeof(struct snps_accel_rproc_priv));
			if (!proc->rprocs[proc->num_rprocs])
				return -ENOMEM;

			core->rproc = proc->rprocs[proc->num_rprocs];

			/* The user-mode libraries control the remote processor */
			proc->rprocs[proc->num_rprocs]->auto_boot = false;

			priv = proc->rprocs[proc->num_rprocs]->priv;
			priv->rw_state = rw_state;

			/* A user-friendly name instead of 'remoteprocX' */
			dev_set_name(&proc->rprocs[i]->dev,
				     "snps_accel_rproc_%u_%u_%u",
				     system->system_id, proc->processor_id,
				     core->core_id);

			priv->fw_type = snps_accel_npx_l2_core(core) ?
					SNPS_ACCEL_FW_TYPE_L2_CORE :
					SNPS_ACCEL_FW_TYPE_CORE;
			priv->proc = proc;
			priv->core = core;

			ret = devm_rproc_add(dev,
					     proc->rprocs[proc->num_rprocs]);
			if (ret) {
				dev_err(dev, "Failed to add rproc #%d: %d\n",
					i, ret);
				return ret;
			}

			proc->num_rprocs++;
		}
	}

	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (!priv->rw_state) {
			ret = snps_accel_ro_rproc_state_create(proc->rprocs[i]);
			if (ret)
				goto out_create_ro;
		}
	}

	dev_dbg(dev, "Processor %u_%u remote processor(s) are probed.\n",
		proc->system->system_id, proc->processor_id);

	return 0;

out_create_ro:
	if (i > 0)
		for (j = i - 1; j >= 0; j++) {
			priv = proc->rprocs[j]->priv;
			if (!priv->rw_state)
				snps_accel_ro_rproc_state_remove(proc->rprocs[j]);
		}

	return ret;
}

/**
 * snps_accel_rprocs_remove - Remove remote processor(s) of a processor.
 * @proc: Target processor.
 */
static void snps_accel_rprocs_remove(struct snps_accel_processor *proc)
{
	struct snps_accel_rproc_priv *priv;
	int i;

	for (i = 0; i < proc->num_rprocs; i++) {
		priv = proc->rprocs[i]->priv;
		if (!priv->rw_state)
			snps_accel_ro_rproc_state_remove(proc->rprocs[i]);
	}

	dev_dbg(&proc->pdev->dev,
		"Processor %u_%u remote processor(s) are removed.\n",
		proc->system->system_id, proc->processor_id);
}

/**
 * snps_accel_proc_probe - Probe a processor's basics.
 * @proc: Target processor.
 * @min_cores: Minimum number of core of the target processor.
 * @max_cores: Maximum number of core of the target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_proc_probe(struct snps_accel_processor *proc,
				 u32 min_cores, u32 max_cores)
{
	struct platform_device *pdev = proc->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct snps_accel_core *core;
	struct resource *res;
	off_t range_offset;
	u32 unused_cores[SNPS_ACCEL_MAX_CORES_PP];
	int ret, i, num_unused_cores;
	const char *reg_name;

	/* Basics */
	ret = of_property_read_u32(node, "snps,accel-processor-id",
				   &proc->processor_id);
	if (ret) {
		dev_err(dev, "Failed to read 'snps,accel-processor-id': %d\n",
			ret);
		return ret;
	}

	if (proc->processor_id >= SNPS_ACCEL_MAX_PROCESSORS_PS) {
		dev_err(dev, "Invalid 'snps,accel-processor-id' %u\n",
			proc->processor_id);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "snps,safety-level",
				   &proc->safety_level);
	if (ret) {
		proc->safety_level = 0;
		dev_info(dev, "Assume 'snps,safety-level': <%u>\n",
			 proc->safety_level);
	}

	ret = of_property_read_u32(node, "snps,csm-size-mb",
				   &proc->csm_size_mb);
	if (ret) {
		proc->csm_size_mb = 0;
		dev_info(dev, "Assume 'snps,csm-size-mb': <%u>\n",
			 proc->csm_size_mb);
	}

	ret = of_property_read_s32(node, "snps,autosuspend-delay-ms",
				   &proc->autosuspend_delay_ms);
	if (ret) {
		proc->autosuspend_delay_ms = 5000;
		dev_info(dev, "Assume 'snps,autosuspend-delay-ms': <%d>\n",
			 proc->autosuspend_delay_ms);
	}

	ret = of_property_read_s32(node, "snps,save-halt-timeout-ms",
				   &proc->save_halt_timeout_ms);
	if (ret) {
		proc->save_halt_timeout_ms = 1000;
		dev_info(dev, "Assume 'snps,save-halt-timeout-ms': <%d>\n",
			 proc->save_halt_timeout_ms);
	}

	proc->bootaddr_64bit = of_property_read_bool(node,
						     "snps,bootaddr-64bit");
	proc->forbid_rpm_suspend =
		of_property_read_bool(node, "snps,forbid-rpm-suspend");
	proc->forbid_rpm_clock =
		of_property_read_bool(node, "snps,forbid-rpm-clock");
	proc->forbid_rpm_cfg =
		of_property_read_bool(node, "snps,forbid-rpm-cfg");
	proc->forbid_rpm_powerdown =
		of_property_read_bool(node, "snps,forbid-rpm-powerdown");

	/* Memories */
	proc->num_mems = of_property_count_strings(node, "reg-names");
	if (proc->num_mems < 0) {
		dev_err(dev, "Failed to count 'reg-names' entries: %d\n",
			proc->num_mems);
		return proc->num_mems;
	}

	proc->mems = devm_kcalloc(dev, proc->num_mems, sizeof(*proc->mems),
				  GFP_KERNEL);
	if (!proc->mems)
		return -ENOMEM;

	ret = snps_accel_parse_ranges(dev, &range_offset);
	if (ret) {
		dev_err(dev, "Failed to parse 'ranges' of parents: %d\n", ret);
		return ret;
	}

	for (i = 0; i < proc->num_mems; i++) {
		ret = of_property_read_string_index(node, "reg-names", i,
						    &reg_name);
		if (ret || !reg_name) {
			dev_err(dev, "Failed to read 'reg-names[%i]'\n", i);
			return ret;
		}

		dev_info(dev, "'reg-names[%i]': %s is probed.\n",
			 i, reg_name);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   reg_name);
		if (!res) {
			dev_err(dev, "Failed to get memory resource %s\n",
				reg_name);
			return -EINVAL;
		}

		if (sysfs_streq(reg_name, "npx_dmi_cfg")) {
			if (!snps_accel_npx(proc)) {
				dev_err(dev, "Only NPX has '%s'\n", reg_name);
				return -EINVAL;
			}

			if (!platform_get_resource_byname(pdev, IORESOURCE_MEM,
							  "npx_dmi")) {
				dev_err(dev, "Missing 'npx_dmi'\n");
				return -EINVAL;
			}

			proc->mems[i].type =
					SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG;
			proc->mems[i].phys_addr = res->start;
			proc->mems[i].dev_addr = res->start + range_offset;
			proc->mems[i].size = resource_size(res);
			proc->mems[i].is_ram = -1;
			proc->mems[i].virt_addr.iomem =
					devm_ioremap_resource(dev, res);
			if (IS_ERR(proc->mems[i].virt_addr.iomem)) {
				dev_err(dev, "Failed to map iomem %pap\n",
					&res->start);
				return PTR_ERR(proc->mems[i].virt_addr.iomem);
			}

			dev_info(dev,
				 "mem[%d]: pa %pa sz %#zx va %pS da %#llx\n",
				 i, &proc->mems[i].phys_addr,
				 proc->mems[i].size,
				 proc->mems[i].virt_addr.iomem,
				 proc->mems[i].dev_addr);

			continue;
		} else if (sysfs_streq(reg_name, "npx_dmi")) {
			if (!snps_accel_npx(proc)) {
				dev_err(dev, "Only NPX has '%s'\n", reg_name);
				return -EINVAL;
			}

			proc->mems[i].type = SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI;
			proc->mems[i].phys_addr = res->start;
			proc->mems[i].dev_addr = res->start + range_offset;
			proc->mems[i].size = resource_size(res);
			proc->mems[i].is_ram = -1;

			dev_info(dev, "mem[%d]: pa %pa sz %#zx da %#llx\n",
				 i, &proc->mems[i].phys_addr,
				 proc->mems[i].size, proc->mems[i].dev_addr);

			continue;
		}

		if (!strncmp(reg_name, "fwmem", strlen("fwmem")))
			proc->mems[i].type = SNPS_ACCEL_PROC_MEM_TYPE_FWMEM;
		if (!strncmp(reg_name, "fwvmem", strlen("fwvmem")))
			proc->mems[i].type = SNPS_ACCEL_PROC_MEM_TYPE_FWVMEM;
		else
			proc->mems[i].type = SNPS_ACCEL_PROC_MEM_TYPE_UNKNOWN;

		proc->mems[i].phys_addr = res->start;
		proc->mems[i].dev_addr = res->start + range_offset;
		proc->mems[i].size = resource_size(res);
		proc->mems[i].is_ram = region_intersects(res->start,
							 resource_size(res),
							 IORESOURCE_SYSTEM_RAM,
							 IORES_DESC_NONE);
		proc->mems[i].virt_addr.mem = devm_memremap(
			dev, res->start, resource_size(res),
			proc->mems[i].is_ram == REGION_INTERSECTS ?
						MEMREMAP_WB : MEMREMAP_WT);
		if (IS_ERR(proc->mems[i].virt_addr.mem)) {
			dev_err(dev, "Failed to map mem %pap\n",
				&res->start);
			return PTR_ERR(proc->mems[i].virt_addr.mem);
		}

		dev_info(dev, "mem[%d]: pa %pa sz %#zx va %pS da %#llx\n",
			 i, &proc->mems[i].phys_addr, proc->mems[i].size,
			 proc->mems[i].virt_addr.mem,
			 proc->mems[i].dev_addr);
	}

	/* Cores */
	ret = of_property_read_u32(node, "snps,num-cores", &proc->num_cores);
	if (ret) {
		dev_err(dev, "Failed to read 'snps,num-cores': %d\n", ret);
		return ret;
	}

	if (proc->num_cores == 0 || proc->num_cores < min_cores ||
	    proc->num_cores > max_cores) {
		dev_err(dev, "Invalid the number of core(s): %u\n",
			proc->num_cores);
		return -EINVAL;
	}

	proc->start_core_id =
		(snps_accel_npx(proc) && (proc->num_cores == 1)) ? 1 : 0;
	for (i = 0; i < proc->start_core_id; i++) {
		core = &proc->cores[i];
		core->unused = true;
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		core->proc = proc;
		core->core_id = i;
		core->unused = false;
	}

	num_unused_cores =
		of_property_count_elems_of_size(node, "snps,unused-core-ids",
						sizeof(u32));
	if (num_unused_cores > 0) {
		if (num_unused_cores > proc->num_cores) {
			dev_err(dev,
				"Invalid num of 'snps,unused-core-ids': %d\n",
				num_unused_cores);
			return -EINVAL;
		}

		ret = of_property_read_u32_array(node, "snps,unused-core-ids",
						 unused_cores,
						 num_unused_cores);
		if (ret) {
			dev_err(dev,
				"Failed to read 'snps,unused-core-ids': %d\n",
				ret);
			return ret;
		}

		for (i = 0; i < num_unused_cores; i++) {
			if (!snps_accel_valid_core_id(proc, unused_cores[i])) {
				dev_warn(dev,
					 "Ignored invalid unused core: <%u>\n",
					 unused_cores[i]);
				continue;
			}

			if (proc->num_cores == 1) {
				dev_warn(dev,
					"Ignored only one core unused: <%u>\n",
					unused_cores[i]);
				continue;
			}

			if (snps_accel_npx_first_l2_core_id(proc,
							    unused_cores[i])) {
				dev_warn(dev,
					 "Ignored first NPX L2 unused: <%u>\n",
					 unused_cores[i]);
				continue;
			}

			proc->cores[unused_cores[i]].unused = true;
		}
	}

	dev_dbg(dev, "Processor %u_%u basics are probed.\n",
		proc->system->system_id, proc->processor_id);

	return 0;
}

/**
 * snps_accel_proc_remove - Remove a processor's basics.
 * @proc: Target processor.
 */
static void snps_accel_proc_remove(struct snps_accel_processor *proc)
{
	dev_dbg(&proc->pdev->dev, "Processor %u_%u basics are removed.\n",
		proc->system->system_id, proc->processor_id);
}

/**
 * snps_accel_npx_probe - Probe an NPX processor.
 * @pdev: Platform device of the NPX processor.
 * @data: Match data of the Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_npx_probe(struct platform_device *pdev, const void *data)
{
	struct device *dev = &pdev->dev;
	struct snps_accel_processor *proc;
	struct snps_accel_system *system;
	int ret;

	if (!strncmp(dev_name(dev), SNPS_ACCEL_CORE_DEV_NAME_PREFIX,
		     strlen(SNPS_ACCEL_CORE_DEV_NAME_PREFIX))) {
		dev_info(dev, "NPX core %s is probed.\n", dev_name(dev));
		return 0;
	}

	ret = snps_accel_verify_lineage(pdev, SNPS_ACCEL_DRV_MATCH_SYSTEM,
					SNPS_ACCEL_DRV_MATCH_ARCSYNC);
	if (ret)
		return ret;

	system = dev_get_drvdata(dev->parent);
	if (!kref_get_unless_zero(&system->refcount)) {
		dev_err(dev, "Attempting to open a stale system\n");
		return -ESTALE;
	}

	proc = snps_accel_devm_proc_alloc(pdev, data,
					  sizeof(struct snps_accel_npx));
	if (!proc) {
		ret = -ENOMEM;
		goto out_drop;
	}

	proc->mgr = dev_get_drvdata(dev->parent->parent);
	proc->system = system;
	platform_set_drvdata(pdev, proc);

	/* Probe accelerator processor */
	ret = snps_accel_proc_probe(proc, SNPS_ACCEL_MIN_CORES_PP,
				    SNPS_ACCEL_NPX_MAX_CORES);
	if (ret)
		goto out_drop;

	/* Probe power domain(s) */
	ret = snps_accel_npx_pd_probe(proc);
	if (ret)
		goto out_drop;

	/* Probe remote processor(s) */
	ret = snps_accel_rprocs_probe(proc);
	if (ret)
		goto out_drop;

	ret = snps_accel_proc_sysfs_create(proc);
	if (ret)
		goto out_drop;

	down_write(&system->procs_lock);
	if (system->procs[proc->processor_id]) {
		up_write(&system->procs_lock);
		snps_accel_proc_sysfs_remove(proc);
		snps_accel_rprocs_remove(proc);
		snps_accel_npx_pd_remove(proc);
		snps_accel_proc_remove(proc);
		dev_err(dev,
			"Probed an NPX processor with a duplicated ID %u\n",
			proc->processor_id);
		ret = -EINVAL;
		goto out_drop;
	}
	system->procs[proc->processor_id] = proc;
	++system->num_procs;
	up_write(&system->procs_lock);

	dev_info(dev, "NPX processor %u_%u is probed.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;

out_drop:
	kref_put(&system->refcount, snps_accel_system_release);
	return ret;
}

/**
 * snps_accel_npx_remove - Remove an NPX processor.
 * @pdev: Platform device of the NPX processor.
 */
void snps_accel_npx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snps_accel_system *system;
	struct snps_accel_processor *proc;
	struct snps_accel_core *core;

	if (strncmp(dev_name(dev), SNPS_ACCEL_CORE_DEV_NAME_PREFIX,
		    strlen(SNPS_ACCEL_CORE_DEV_NAME_PREFIX))) {
		proc = dev_get_drvdata(dev);
		system = proc->system;

		down_write(&system->procs_lock);
		snps_accel_proc_sysfs_remove(proc);
		snps_accel_rprocs_remove(proc);
		snps_accel_npx_pd_remove(proc);
		snps_accel_proc_remove(proc);
		system->procs[proc->processor_id] = NULL;
		--system->num_procs;
		up_write(&system->procs_lock);
		kref_put(&system->refcount, snps_accel_system_release);

		dev_info(dev, "NPX processor %u_%u is removed.\n",
			 system->system_id, proc->processor_id);
	} else {
		core = dev_get_drvdata(dev);
		proc = core->proc;

		dev_info(dev, "NPX core %u_%u_%u is removed.\n",
			 proc->system->system_id, proc->processor_id,
			 core->core_id);
	}
}

/**
 * snps_accel_vpx_probe - Probe a VPX processor.
 * @pdev: Platform device of the VPX processor.
 * @data: Match data of the Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_vpx_probe(struct platform_device *pdev, const void *data)
{
	struct device *dev = &pdev->dev;
	struct snps_accel_processor *proc;
	struct snps_accel_system *system;
	int ret;

	if (!strncmp(dev_name(dev), SNPS_ACCEL_CORE_DEV_NAME_PREFIX,
		     strlen(SNPS_ACCEL_CORE_DEV_NAME_PREFIX))) {
		dev_info(dev, "VPX core %s is probed.\n", dev_name(dev));
		return 0;
	}

	ret = snps_accel_verify_lineage(pdev, SNPS_ACCEL_DRV_MATCH_SYSTEM,
					SNPS_ACCEL_DRV_MATCH_ARCSYNC);
	if (ret)
		return ret;

	system = dev_get_drvdata(dev->parent);
	if (!kref_get_unless_zero(&system->refcount)) {
		dev_err(dev, "Attempting to open a stale system\n");
		return -ESTALE;
	}

	proc = snps_accel_devm_proc_alloc(pdev, data,
					  sizeof(struct snps_accel_vpx));
	if (!proc) {
		ret = -ENOMEM;
		goto out_drop;
	}

	proc->mgr = dev_get_drvdata(dev->parent->parent);
	proc->system = system;
	platform_set_drvdata(pdev, proc);

	/* Probe accelerator processor */
	ret = snps_accel_proc_probe(proc, SNPS_ACCEL_MIN_CORES_PP,
				    SNPS_ACCEL_VPX_MAX_CORES);
	if (ret)
		goto out_drop;

	/* Probe power domain(s) */
	ret = snps_accel_vpx_pd_probe(proc);
	if (ret)
		goto out_drop;

	/* Probe and remote processor(s) */
	ret = snps_accel_rprocs_probe(proc);
	if (ret)
		goto out_drop;

	ret = snps_accel_proc_sysfs_create(proc);
	if (ret)
		goto out_drop;

	down_write(&system->procs_lock);
	if (system->procs[proc->processor_id]) {
		up_write(&system->procs_lock);
		snps_accel_proc_sysfs_remove(proc);
		snps_accel_rprocs_remove(proc);
		snps_accel_vpx_pd_remove(proc);
		snps_accel_proc_remove(proc);
		dev_err(dev,
			"Probed a VPX processor with a duplicated ID %u\n",
			proc->processor_id);
		ret = -EINVAL;
		goto out_drop;
	}
	system->procs[proc->processor_id] = proc;
	++system->num_procs;
	up_write(&system->procs_lock);

	dev_info(dev, "VPX processor %u_%u is probed.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;

out_drop:
	kref_put(&system->refcount, snps_accel_system_release);
	return ret;
}

/**
 * snps_accel_vpx_remove - Remove a VPX processor.
 * @pdev: Platform device of the VPX processor.
 */
void snps_accel_vpx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snps_accel_system *system;
	struct snps_accel_processor *proc;
	struct snps_accel_core *core;

	if (strncmp(dev_name(dev), SNPS_ACCEL_CORE_DEV_NAME_PREFIX,
		    strlen(SNPS_ACCEL_CORE_DEV_NAME_PREFIX))) {
		proc = dev_get_drvdata(dev);
		system = proc->system;

		down_write(&system->procs_lock);
		snps_accel_proc_sysfs_remove(proc);
		snps_accel_rprocs_remove(proc);
		snps_accel_vpx_pd_remove(proc);
		snps_accel_proc_remove(proc);
		system->procs[proc->processor_id] = NULL;
		--system->num_procs;
		up_write(&system->procs_lock);
		kref_put(&system->refcount, snps_accel_system_release);

		dev_info(dev, "VPX processor %u_%u is removed.\n",
			 system->system_id, proc->processor_id);
	} else {
		core = dev_get_drvdata(dev);
		proc = core->proc;

		dev_info(dev, "VPX core %u_%u_%u is removed.\n",
			 proc->system->system_id, proc->processor_id,
			 core->core_id);
	}
}
