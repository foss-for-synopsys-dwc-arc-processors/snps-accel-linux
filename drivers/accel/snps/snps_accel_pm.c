// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include "snps_accel_drv.h"

/**
 * snps_accel_cores_rpm_get - Runtime PM get multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @cores_wait: Flags to show if a core needs to wait for completion.
 * @num_cw: Number of flags used for showing if cores need to wait for completion.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_cores_rpm_get(struct snps_accel_system *system,
			     struct snps_accel_ioctl_rpm_cmd *data,
			     bool *cores_wait, u32 num_cw, int rpmflags)
{
	struct snps_accel_processor *proc;
	struct snps_accel_core *core;
	struct device *dev;
	u32 processor_id, core_id;
	int i, ret = 0;

	BUG_ON(data->num_cores > num_cw);

	pr_debug("%s: Processing runtime PM get of %u cores.\n",
		 __func__, data->num_cores);

	for (i = 0; i < data->num_cores; i++) {
		processor_id = data->cores[i].id >> data->core_id_bw;
		core_id = data->cores[i].id & ((1U << data->core_id_bw) - 1);
		if (unlikely(processor_id >= SNPS_ACCEL_MAX_PROCESSORS_PS)) {
			pr_err("%s: Invalid processor %u_%u.\n",
			       __func__, system->system_id, processor_id);
			data->cores[i].result = -EINVAL;
			ret = -EINVAL;
			continue;
		}

		proc = system->procs[processor_id];
		if (unlikely(!proc)) {
			pr_err("%s: Cannot get processor %u_%u.\n",
			       __func__, system->system_id, processor_id);
			data->cores[i].result = -ENODEV;
			ret = -ENODEV;
			continue;
		}

		if (unlikely(!snps_accel_valid_core_id(proc, core_id))) {
			dev_err(&proc->pdev->dev, "Invalid core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			data->cores[i].result = -EINVAL;
			ret = -EINVAL;
			continue;
		}

		core = &proc->cores[core_id];
		if (unlikely(core->unused)) {
			dev_err(&proc->pdev->dev,
				"Cannot get unused core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			data->cores[i].result = -ENODEV;
			ret = -ENODEV;
			continue;
		}

		if (snps_accel_npx_l2_core(core)) {
			dev_dbg(&proc->pdev->dev,
				"No need to get L2 core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			data->cores[i].result = 0;
			continue;
		}

		if (unlikely(core->rproc->state != RPROC_RUNNING &&
			     core->rproc->state != RPROC_ATTACHED)) {
			dev_err(&proc->pdev->dev, "Invalid rproc state %u.\n",
				core->rproc->state);
			data->cores[i].result = -ENODEV;
			ret = -ENODEV;
			continue;
		}

		dev = &core->pdev->dev;

		if (unlikely((rpmflags & RPM_ASYNC) == 0)) {
			data->cores[i].result = pm_runtime_resume_and_get(dev);
			if (unlikely(data->cores[i].result < 0)) {
				dev_err(&proc->pdev->dev,
					"Failed to get core %u_%u_%u: %d\n",
					system->system_id, processor_id,
					core_id, data->cores[i].result);
				ret = -EAGAIN;
			}

			continue;
		}

		data->cores[i].result = pm_runtime_get(dev);
		if (likely(data->cores[i].result > 0)) {
			/* Already powered up */
			dev_dbg(&proc->pdev->dev,
				"Got powered-up core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			continue;
		} else if (unlikely(data->cores[i].result < 0)) {
			/* Cannot power up */
			dev_err(&proc->pdev->dev,
				"Failed to get core %u_%u_%u async: %d.\n",
				system->system_id, processor_id,
				core_id, data->cores[i].result);
			pm_runtime_put_noidle(dev);
			ret = -EAGAIN;
			continue;
		}

		dev_dbg(&proc->pdev->dev, "Get core %u_%u_%u async.\n",
			system->system_id, processor_id, core_id);
		cores_wait[i] = true;
	}

	if (ret) {
		for (i = 0; i < data->num_cores; i++) {
			if (data->cores[i].result < 0)
				continue;

			processor_id = data->cores[i].id >> data->core_id_bw;
			core_id = data->cores[i].id &
				  ((1U << data->core_id_bw) - 1);
			proc = system->procs[processor_id];
			core = &proc->cores[core_id];
			dev = &core->pdev->dev;
			pm_runtime_put_autosuspend(dev);
		}

		return ret;
	}

	return 0;
}

/**
 * snps_accel_cores_rpm_wait - Wait for completion of Runtime PM get multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @cores_wait: Flags to show if a core needs to wait for completion.
 * @num_cw: Number of flags used for showing if cores need to wait for completion.
 */
void snps_accel_cores_rpm_wait(struct snps_accel_system *system,
			       struct snps_accel_ioctl_rpm_cmd *data,
			       bool *cores_wait, u32 num_cw)
{
	struct snps_accel_processor *proc;
	struct snps_accel_core *core;
	struct device *dev;
	u32 processor_id, core_id;
	int i;

	BUG_ON(data->num_cores > num_cw);

	if (unlikely(num_cw == 0))
		return;

	pr_debug("%s: Processing runtime PM wait of %u cores.\n",
		 __func__, data->num_cores);

	for (i = 0; i < data->num_cores; i++) {
		if (!cores_wait[i])
			continue;

		processor_id = data->cores[i].id >> data->core_id_bw;
		core_id = data->cores[i].id & ((1U << data->core_id_bw) - 1);
		proc = system->procs[processor_id];
		core = &proc->cores[core_id];
		dev = &core->pdev->dev;

		flush_work(&dev->power.work);
		if (!pm_runtime_active(dev)) {
			data->cores[i].result = -EAGAIN;
			dev_err(&proc->pdev->dev,
				"Failed to get core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			continue;
		}

		dev_dbg(&proc->pdev->dev, "Got core %u_%u_%u.\n",
			 system->system_id, processor_id, core_id);
	}
}

/**
 * snps_accel_cores_rpm_put - Runtime PM put multiple cores at once.
 * @system: System.
 * @data: Runtime PM command.
 * @rpmflags: Runtime PM flag argument bits.
 */
int snps_accel_cores_rpm_put(struct snps_accel_system *system,
			      struct snps_accel_ioctl_rpm_cmd *data,
			      int rpmflags)
{
	struct snps_accel_processor *proc;
	struct snps_accel_core *core;
	struct device *dev;
	u32 processor_id, core_id;
	int i, ret = 0;

	pr_debug("%s: Processing runtime PM put of %u cores.\n",
		 __func__, data->num_cores);

	for (i = 0; i < data->num_cores; i++) {
		processor_id = data->cores[i].id >> data->core_id_bw;
		core_id = data->cores[i].id & ((1U << data->core_id_bw) - 1);
		if (unlikely(processor_id >= SNPS_ACCEL_MAX_PROCESSORS_PS)) {
			pr_err("%s: Invalid processor %u_%u.\n",
			       __func__, system->system_id, processor_id);
			data->cores[i].result = -EINVAL;
			ret = -EINVAL;
			continue;
		}

		proc = system->procs[processor_id];
		if (unlikely(!proc)) {
			pr_err("%s: Cannot get processor %u_%u.\n",
			       __func__, system->system_id, processor_id);
			data->cores[i].result = -ENODEV;
			ret = -ENODEV;
			continue;
		}

		if (unlikely(!snps_accel_valid_core_id(proc, core_id))) {
			dev_err(&proc->pdev->dev,
				"Invalid core ID %u_%u_%u.\n",
				system->system_id, processor_id,
				core_id);
			data->cores[i].result = -EINVAL;
			ret = -EINVAL;
			continue;
		}

		core = &proc->cores[core_id];
		if (unlikely(core->unused)) {
			dev_err(&proc->pdev->dev,
				"Cannot put unused core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			data->cores[i].result = -ENODEV;
			ret = -ENODEV;
			continue;
		}

		if (snps_accel_npx_l2_core(core)) {
			dev_dbg(&proc->pdev->dev,
				"No need to put L2 core %u_%u_%u.\n",
				system->system_id, processor_id, core_id);
			data->cores[i].result = 0;
			continue;
		}

		dev = &core->pdev->dev;

		if (unlikely((rpmflags & RPM_ASYNC) == 0 &&
			     (rpmflags & RPM_AUTO) == 0)) {
			data->cores[i].result =
				pm_runtime_put_sync_suspend(dev);
			if (unlikely(data->cores[i].result < 0)) {
				dev_err(&proc->pdev->dev,
					"Cannot put core %u_%u_%u: %d\n",
					system->system_id, processor_id,
					core_id, data->cores[i].result);
				ret = data->cores[i].result;
			}

			continue;
		}

		if (unlikely((rpmflags & RPM_ASYNC) == 0 &&
			     (rpmflags & RPM_AUTO) != 0)) {
			data->cores[i].result =
				pm_runtime_put_sync_autosuspend(dev);
			if (unlikely(data->cores[i].result < 0)) {
				dev_err(&proc->pdev->dev,
					"Cannot put core %u_%u_%u auto: %d\n",
					system->system_id, processor_id,
					core_id, data->cores[i].result);
				ret = data->cores[i].result;
			}

			continue;
		}

		if (unlikely((rpmflags & RPM_AUTO) == 0)) {
			data->cores[i].result = pm_runtime_put(dev);
			if (unlikely(data->cores[i].result < 0)) {
				dev_err(&proc->pdev->dev,
					"Cannot put core %u_%u_%u async: %d\n",
					system->system_id, processor_id,
					core_id, data->cores[i].result);
				ret = data->cores[i].result;
			}

			continue;
		}

		pm_runtime_mark_last_busy(dev);
		data->cores[i].result = pm_runtime_put_autosuspend(dev);
		if (unlikely(data->cores[i].result < 0)) {
			dev_err(&proc->pdev->dev,
				"Cannot put core %u_%u_%u auto async: %d\n",
				system->system_id, processor_id, core_id,
				data->cores[i].result);
			ret = data->cores[i].result;
		}
	}

	if (ret) {
		for (i = 0; i < data->num_cores; i++) {
			if (data->cores[i].result < 0)
				continue;

			processor_id = data->cores[i].id >> data->core_id_bw;
			core_id = data->cores[i].id &
				  ((1U << data->core_id_bw) - 1);
			proc = system->procs[processor_id];
			core = &proc->cores[core_id];
			dev = &core->pdev->dev;
			pm_runtime_resume_and_get(dev);
		}

		return ret;
	}

	return 0;
}

/**
 * snps_accel_arcsync_power_on - Power on callback of a manager (ARCSync).
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_power_on(struct generic_pm_domain *genpd)
{
	struct snps_accel_mgr *mgr =
		container_of(genpd, struct snps_accel_mgr, genpd);

	if (mgr->custom_ops && mgr->custom_ops->set_mgr_power_mode)
		mgr->custom_ops->set_mgr_power_mode(mgr->mgr_id,
						    SNPS_ACCEL_PMODE_UP,
						    mgr->custom_ops_data);

	dev_dbg(mgr->dev, "Manager %u PD is powered on.\n", mgr->mgr_id);

	return 0;
}

/**
 * snps_accel_arcsync_power_off - Power off callback of a manager (ARCSync).
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_power_off(struct generic_pm_domain *genpd)
{
	struct snps_accel_mgr *mgr =
		container_of(genpd, struct snps_accel_mgr, genpd);

	if (mgr->custom_ops && mgr->custom_ops->set_mgr_power_mode)
		mgr->custom_ops->set_mgr_power_mode(mgr->mgr_id,
						    SNPS_ACCEL_PMODE_DOWN,
						    mgr->custom_ops_data);

	dev_dbg(mgr->dev, "Manager %u PD is powered off.\n", mgr->mgr_id);

	return 0;
}

/**
 * snps_accel_npx_proc_power_on - Power on callback of an NPX processor.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_npx_proc_power_on(struct generic_pm_domain *genpd)
{
	struct snps_accel_processor *proc =
		container_of(genpd, struct snps_accel_processor, genpd);
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_core *core;
	int i;

	if (proc->num_cores == 1)
		goto out;

	/* Deassert reset of L2 core group */
	if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_l2_core_group_reset(mgr->dev, proc->processor_id,
						  false);

	/* Power up L2 core group */
	if (mgr->custom_ops && mgr->custom_ops->set_core_power_mode)
		mgr->custom_ops->set_core_power_mode(proc->processor_id, 0,
						     SNPS_ACCEL_PMODE_UP,
						     mgr->custom_ops_data);
	else
		mgr->ops->set_core_power_mode(mgr->dev, proc->processor_id,
					      0, SNPS_ACCEL_PMODE_UP,
					      !proc->forbid_rpm_cfg);

	for (i = 0; i < npx->num_l2_cores; i++) {
		core = npx->l2_cores[i];
		if (core->unused)
			continue;

		/* Enable clock of L2 core */
		if (!proc->forbid_rpm_clock)
			mgr->ops->set_core_clock(mgr->dev, proc->processor_id,
						 core->core_id, true);
	}

	/* Configure L2 core group */
	if (!proc->forbid_rpm_cfg)
		snps_accel_npx_cfg_l2_core_group(proc);

out:
	dev_info(dev, "NPX processor %u_%u PD is powered on.\n",
		proc->system->system_id, proc->processor_id);

	return 0;
}

/**
 * snps_accel_npx_proc_power_off - Power off callback of an NPX processor.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_npx_proc_power_off(struct generic_pm_domain *genpd)
{
	struct snps_accel_processor *proc =
		container_of(genpd, struct snps_accel_processor, genpd);
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_core *core;
	int i;

	if (proc->num_cores == 1)
		goto out;

	/* Disable clock of L2 core group */
	if (!proc->forbid_rpm_clock) {
		for (i = 0; i < npx->num_l2_cores; i++) {
			core = npx->l2_cores[i];
			if (core->unused)
				continue;

			mgr->ops->set_core_clock(mgr->dev, proc->processor_id,
						 core->core_id, false);
		}
	}

	/* Power down L2 core group */
	if (mgr->custom_ops && mgr->custom_ops->set_core_power_mode)
		mgr->custom_ops->set_core_power_mode(proc->processor_id, 0,
						     SNPS_ACCEL_PMODE_DOWN,
						     mgr->custom_ops_data);
	else if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_core_power_mode(mgr->dev, proc->processor_id,
					      0, SNPS_ACCEL_PMODE_DOWN, false);

	/* Assert reset of L2 core group */
	if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_l2_core_group_reset(mgr->dev, proc->processor_id,
						  true);

out:
	dev_info(dev, "NPX processor %u_%u PD is powered off.\n",
		proc->system->system_id, proc->processor_id);

	return 0;
}

/**
 * snps_accel_npx_core_group_power_on - Power on callback of an NPX core group.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_npx_core_group_power_on(struct generic_pm_domain *genpd)
{
	struct snps_accel_core_group *group =
		container_of(genpd, struct snps_accel_core_group, genpd);
	struct snps_accel_processor *proc = group->proc;
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_mgr *mgr = proc->mgr;

	/* Deassert the core group reset */
	if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_core_group_reset(mgr->dev, proc->processor_id,
					       group->group_id, false);

	/* Power up */
	if (mgr->custom_ops && mgr->custom_ops->set_core_group_power_mode)
		mgr->custom_ops->set_core_group_power_mode(proc->processor_id,
							   group->group_id,
							   SNPS_ACCEL_PMODE_UP,
							   mgr->custom_ops_data);
	else
		mgr->ops->set_core_group_power_mode(mgr->dev, proc->processor_id,
						    group->group_id,
						    SNPS_ACCEL_PMODE_UP,
						    !proc->forbid_rpm_cfg);

	/* Enable clock of the core group */
	if (!proc->forbid_rpm_clock)
		mgr->ops->set_core_group_clock(mgr->dev, proc->processor_id,
					       group->group_id, true);

	/* Configure the core group */
	if (!proc->forbid_rpm_cfg)
		snps_accel_npx_cfg_core_group(group);

	dev_info(dev, "NPX core group %u_%u_%u PD is powered on.\n",
		proc->system->system_id, proc->processor_id, group->group_id);

	return 0;
}

/**
 * snps_accel_npx_core_group_power_off - Power off callback of an NPX core group.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_npx_core_group_power_off(struct generic_pm_domain *genpd)
{
	struct snps_accel_core_group *group =
		container_of(genpd, struct snps_accel_core_group, genpd);
	struct snps_accel_processor *proc = group->proc;
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_mgr *mgr = proc->mgr;

	/* Disable clock of the core group */
	if (!proc->forbid_rpm_clock)
		mgr->ops->set_core_group_clock(mgr->dev, proc->processor_id,
					       group->group_id, false);

	/* Power down of the core group */
	if (mgr->custom_ops && mgr->custom_ops->set_core_group_power_mode)
		mgr->custom_ops->set_core_group_power_mode(proc->processor_id,
							   group->group_id,
							   SNPS_ACCEL_PMODE_DOWN,
							   mgr->custom_ops_data);
	else if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_core_group_power_mode(mgr->dev,
						    proc->processor_id,
						    group->group_id,
						    SNPS_ACCEL_PMODE_DOWN, false);

	/* Assert reset of the core group */
	if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_core_group_reset(mgr->dev, proc->processor_id,
					       group->group_id, true);

	dev_info(dev, "NPX core group %u_%u_%u PD is powered off.\n",
		proc->system->system_id, proc->processor_id, group->group_id);

	return 0;
}

static int snps_accel_core_resume_top(struct snps_accel_core *core)
{
	struct snps_accel_processor *proc = core->proc;
	struct snps_accel_mgr *mgr = proc->mgr;

	if (mgr->custom_ops && mgr->custom_ops->set_core_power_mode)
		mgr->custom_ops->set_core_power_mode(
			proc->processor_id, core->core_id,
			SNPS_ACCEL_PMODE_UP, mgr->custom_ops_data);
	else
		mgr->ops->set_core_power_mode(mgr->dev,
					      proc->processor_id,
					      core->core_id,
					      SNPS_ACCEL_PMODE_UP, true);

	if (!proc->forbid_rpm_clock)
		mgr->ops->set_core_clock(mgr->dev, proc->processor_id,
					 core->core_id, true);

	return 0;
}

static void snps_accel_core_resume_bottom(struct snps_accel_core *core)
{
	struct snps_accel_processor *proc = core->proc;
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_system *system = proc->system;
#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	int ret;
	unsigned long timeout;
	u32 core_status;
#endif

	if (!proc->forbid_rpm_powerdown)
		mgr->ops->reset_core(mgr->dev, proc->processor_id,
				     core->core_id);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	ret = mgr->ops->get_core_status(mgr->dev, proc->processor_id,
					core->core_id, &core_status);
	if (ret || snps_accel_core_running(core_status)) {
		dev_err(dev, "Failed to get core %u_%u_%u status %u: %d\n",
			system->system_id, proc->processor_id,
			core->core_id, core_status, ret);
		BUG_ON(1);
	}
#endif

	mgr->ops->run_core(mgr->dev, proc->processor_id, core->core_id, false);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	timeout = jiffies + msecs_to_jiffies(1000);
	do {
		ret = mgr->ops->get_core_status(mgr->dev, proc->processor_id,
						core->core_id, &core_status);
		BUG_ON(ret);

		if (!snps_accel_core_halted(core_status))
			break;

		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, timeout));

	if (snps_accel_core_halted(core_status)) {
		dev_err(dev, "Core %u_%u_%u (status %u) is not running.\n",
			system->system_id, proc->processor_id,
			core->core_id, core_status);
		BUG_ON(1);
	}
#endif

	dev_info(dev, "Core %u_%u_%u is resumed.\n", system->system_id,
		proc->processor_id, core->core_id);
}

static int snps_accel_core_suspend_top(struct snps_accel_core *core)
{
	struct snps_accel_processor *proc = core->proc;
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_system *system = proc->system;
	struct snps_accel_mbox_msg msg;
	unsigned long timeout;
	u32 core_status;
	int ret;

	if (proc->forbid_rpm_powerdown)
		goto out_halt;

	/* Ask the core's firmware to do a save-and-halt */
	msg.processor_id = proc->processor_id;
	msg.core_id = core->core_id;

	do {
		ret = snps_accel_mbox_send_message(system, &msg);
		if (ret == 0)
			break;

		schedule_timeout_uninterruptible(1);
	} while (true);

	dev_dbg(dev, "Putting a msg to core %u_%u_%u\n",
		system->system_id, proc->processor_id, core->core_id);

	/* Check if the core is halted */
	timeout = jiffies + msecs_to_jiffies(proc->save_halt_timeout_ms);
	do {
		ret = mgr->ops->get_core_status(mgr->dev, proc->processor_id,
						core->core_id, &core_status);
#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
		BUG_ON(ret);
#endif

		if (snps_accel_core_halted(core_status)) {
			dev_dbg(dev, "Core %u_%u_%u is halted gracefully.\n",
				system->system_id, proc->processor_id,
				core->core_id);
			return 0;
		}

		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, timeout));

	snps_accel_mbox_abort_message(system, &msg);

	dev_warn(dev, "Core %u_%u_%u is going to be halted forcefully.\n",
		 system->system_id, proc->processor_id, core->core_id);

out_halt:
	ret = mgr->ops->halt_core(mgr->dev, proc->processor_id, core->core_id,
				  false);
#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	BUG_ON(ret);
	ret = mgr->ops->get_core_status(mgr->dev, proc->processor_id,
					core->core_id, &core_status);
	if (ret || !snps_accel_core_halted(core_status)) {
		dev_err(dev, "Failed to get core status %u: %d\n",
			core_status, ret);
		BUG_ON(1);
	}
#endif

	return 0;
}

static void snps_accel_core_suspend_bottom(struct snps_accel_core *core)
{
	struct snps_accel_processor *proc = core->proc;
	struct device *dev = &proc->pdev->dev;
	struct snps_accel_mgr *mgr = proc->mgr;
	struct snps_accel_system *system = proc->system;

	if (!proc->forbid_rpm_clock)
		mgr->ops->set_core_clock(mgr->dev, proc->processor_id,
					 core->core_id, false);

	if (mgr->custom_ops && mgr->custom_ops->set_core_power_mode)
		mgr->custom_ops->set_core_power_mode(proc->processor_id,
						     core->core_id,
						     SNPS_ACCEL_PMODE_DOWN,
						     mgr->custom_ops_data);
	else if (!proc->forbid_rpm_powerdown)
		mgr->ops->set_core_power_mode(mgr->dev, proc->processor_id,
					      core->core_id,
					      SNPS_ACCEL_PMODE_DOWN, false);

	dev_info(dev, "Core %u_%u_%u is suspended.\n", system->system_id,
		proc->processor_id, core->core_id);
}

/**
 * snps_accel_l2_core_power_on - Power on callback of a L2 core.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_l2_core_power_on(struct generic_pm_domain *genpd)
{
	struct snps_accel_core *core =
		container_of(genpd, struct snps_accel_core, genpd);

	snps_accel_core_resume_bottom(core);
	return 0;
}

/**
 * snps_accel_l2_core_power_off - Power off callback of a L2 core.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_l2_core_power_off(struct generic_pm_domain *genpd)
{
	struct snps_accel_core *core =
		container_of(genpd, struct snps_accel_core, genpd);

	return snps_accel_core_suspend_top(core);
}

/**
 * snps_accel_runtime_suspend - Runtime suspend callback of a core.
 * @dev: Device of a core.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_runtime_suspend(struct device *dev)
{
	struct snps_accel_core *core = dev_get_drvdata(dev);
	int ret;

	/* Primary core is dealt with at processor-level */
	if (core->core_id == 0)
		return 0;

	ret = snps_accel_core_suspend_top(core);
	if (ret)
		return ret;

	snps_accel_core_suspend_bottom(core);
	return 0;
}

/**
 * snps_accel_runtime_resume - Runtime resume callback of a core.
 * @dev: Device of a core.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_runtime_resume(struct device *dev)
{
	struct snps_accel_core *core = dev_get_drvdata(dev);
	int ret;

	/* Primary core is dealt with at processor-level */
	if (core->core_id == 0)
		return 0;

	ret = snps_accel_core_resume_top(core);
	if (ret)
		return ret;

	snps_accel_core_resume_bottom(core);
	return 0;
}

/**
 * snps_accel_l2_core_pd_probe - Probe a L2 core's PM domain.
 * @parent: Parent generic PM domain.
 * @core: Target core.
 * @power_off: Power-off callback of generic power domain.
 * @power_on: Power-on callback of generic power domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_l2_core_pd_probe(struct generic_pm_domain *parent,
			struct snps_accel_core *core,
			int (*power_off)(struct generic_pm_domain *domain),
			int (*power_on)(struct generic_pm_domain *domain))
{
	struct snps_accel_processor *proc = core->proc;
	struct device *proc_dev = &proc->pdev->dev;
	int ret;

	if (core->unused)
		return 0;

	core->genpd.name = devm_kasprintf(proc_dev, GFP_KERNEL,
				SNPS_ACCEL_CORE_DEV_NAME_PREFIX"_%u_%u_%u_pd",
				proc->system->system_id, proc->processor_id,
				core->core_id);
	core->genpd.power_off = power_off;
	core->genpd.power_on = power_on;

	ret = pm_genpd_init(&core->genpd, NULL, true);
	if (ret) {
		dev_err(proc_dev, "Failed to init domain %s: %d\n",
			core->genpd.name, ret);
		return ret;
	}

	ret = pm_genpd_add_subdomain(parent, &core->genpd);
	if (ret) {
		pm_genpd_remove(&core->genpd);
		dev_err(proc_dev,
			"Failed to add subdomain %s to parent %s: %d\n",
			core->genpd.name, parent->name, ret);
		return ret;
	}

	core->pdev = NULL;

	dev_dbg(proc_dev, "L2 core %u_%u_%u PD is probed.\n",
		proc->system->system_id, proc->processor_id, core->core_id);

	return 0;
}

/**
 * snps_accel_core_pd_remove - Remove a core's PM domain.
 * @parent: Parent generic PM domain.
 * @core: Target core.
 */
static void snps_accel_l2_core_pd_remove(struct generic_pm_domain *parent,
					 struct snps_accel_core *core)
{
	if (!core->unused) {
		pm_genpd_remove_subdomain(parent, &core->genpd);
		pm_genpd_remove(&core->genpd);

		dev_dbg(&core->proc->pdev->dev,
			"Core %u_%u_%u PD is removed.\n",
			core->proc->system->system_id,
			core->proc->processor_id, core->core_id);
	}
}

/**
 * snps_accel_core_pd_probe - Probe a core's PM domain.
 * @parent: Parent generic PM domain.
 * @core: Target core.
 * @power_off: Power-off callback of generic power domain.
 * @power_on: Power-on callback of generic power domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_core_pd_probe(struct generic_pm_domain *parent,
			struct snps_accel_core *core,
			int (*power_off)(struct generic_pm_domain *domain),
			int (*power_on)(struct generic_pm_domain *domain))
{
	struct snps_accel_processor *proc = core->proc;
	struct device *proc_dev = &proc->pdev->dev;
	struct device *core_dev;
	char *core_dev_name;
	int ret;

	if (core->unused)
		return 0;

	core_dev_name = devm_kasprintf(proc_dev, GFP_KERNEL,
				SNPS_ACCEL_CORE_DEV_NAME_PREFIX"_%u_%u_%u",
				proc->system->system_id, proc->processor_id,
				core->core_id);
	core->pdev = platform_device_alloc(core_dev_name, -1);
	if (!core->pdev)
		return -ENOMEM;

	core_dev = &core->pdev->dev;
	core_dev->parent = proc_dev;

	/* Ensure the driver is bound to the core device */
	core_dev->of_node = proc_dev->of_node;

	core->genpd.name = devm_kasprintf(proc_dev, GFP_KERNEL,
				SNPS_ACCEL_CORE_DEV_NAME_PREFIX"_%u_%u_%u_pd",
				proc->system->system_id, proc->processor_id,
				core->core_id);
	core->genpd.power_off = power_off;
	core->genpd.power_on = power_on;

	ret = pm_genpd_init(&core->genpd, NULL, true);
	if (ret) {
		dev_err(proc_dev, "Failed to init domain %s: %d\n",
			core->genpd.name, ret);
		goto out_genpd_init;
	}

	ret = pm_genpd_add_device(&core->genpd, core_dev);
	if (ret) {
		dev_err(proc_dev,
			"Failed to add core %u_%u_%u to PM domain %s: %d\n",
			proc->system->system_id, proc->processor_id,
			core->core_id, core->genpd.name, ret);
		goto out_add_device;
	}

	platform_set_drvdata(core->pdev, core);
	ret = platform_device_add(core->pdev);
	if (ret) {
		dev_err(proc_dev,
			"Failed to add device for core %u_%u_%u: %d\n",
			proc->system->system_id, proc->processor_id,
			core->core_id, ret);
		goto out_pdev_add;
	}

	ret = pm_genpd_add_subdomain(parent, &core->genpd);
	if (ret) {
		dev_err(proc_dev,
			"Failed to add subdomain %s to parent %s: %d\n",
			core->genpd.name, parent->name, ret);
		goto out_add_subdomain;
	}

	pm_runtime_use_autosuspend(core_dev);
	pm_runtime_set_autosuspend_delay(core_dev,
					 proc->autosuspend_delay_ms);

	dev_dbg(proc_dev, "Core %u_%u_%u PD is probed.\n",
		proc->system->system_id, proc->processor_id, core->core_id);

	return 0;

out_add_subdomain:
	platform_device_del(core->pdev);

out_pdev_add:
	pm_genpd_remove_device(core_dev);

out_add_device:
	pm_genpd_remove(&core->genpd);

out_genpd_init:
	platform_device_put(core->pdev);
	core->pdev = NULL;
	return ret;
}

/**
 * snps_accel_core_pd_remove - Remove a core's PM domain.
 * @parent: Parent generic PM domain.
 * @core: Target core.
 */
static void snps_accel_core_pd_remove(struct generic_pm_domain *parent,
				      struct snps_accel_core *core)
{
	if (!core->unused) {
		pm_genpd_remove_subdomain(parent, &core->genpd);
		platform_device_del(core->pdev);
		pm_genpd_remove_device(&core->pdev->dev);
		pm_genpd_remove(&core->genpd);
		platform_device_put(core->pdev);
		core->pdev = NULL;

		dev_dbg(&core->proc->pdev->dev,
			"Core %u_%u_%u PD is removed.\n",
			core->proc->system->system_id,
			core->proc->processor_id, core->core_id);
	}
}

/**
 * snps_accel_proc_pd_probe - Probe a processor's PM domain.
 * @parent: Parent generic PM domain.
 * @proc: Target processor.
 * @power_off: Power off callback.
 * @power_on: Power on callback.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_proc_pd_probe(struct generic_pm_domain *parent,
			struct snps_accel_processor *proc,
			int (*power_off)(struct generic_pm_domain *domain),
			int (*power_on)(struct generic_pm_domain *domain))
{
	int ret;

	/* Add the processor to its manager's PM domain */
	proc->genpd.name = devm_kasprintf(&proc->pdev->dev, GFP_KERNEL,
					  "snps_accel_proc_%u_%u_pd",
					  proc->system->system_id,
					  proc->processor_id);
	proc->genpd.power_off = power_off;
	proc->genpd.power_on = power_on;

	ret = pm_genpd_init(&proc->genpd, NULL, true);
	if (ret) {
		dev_err(&proc->pdev->dev, "Failed to init domain %s: %d\n",
			proc->genpd.name, ret);
		return ret;
	}

	if (parent) {
		ret = pm_genpd_add_subdomain(parent, &proc->genpd);
		if (ret) {
			dev_err(&proc->pdev->dev,
				"Failed to add subdomain %s to parent %s: %d\n",
				proc->genpd.name, parent->name, ret);
			goto out_add;
		}
	}

	dev_dbg(&proc->pdev->dev, "Processor %u_%u PD is probed.\n",
		proc->system->system_id, proc->processor_id);

	return 0;

out_add:
	pm_genpd_remove(&proc->genpd);
	return ret;
}

/**
 * snps_accel_proc_pd_remove - Remove a processor's PM domain.
 * @parent: Parent generic PM domain.
 * @proc: Target processor.
 */
static void snps_accel_proc_pd_remove(struct generic_pm_domain *parent,
				      struct snps_accel_processor *proc)
{
	if (parent)
		pm_genpd_remove_subdomain(parent, &proc->genpd);

	pm_genpd_remove(&proc->genpd);

	dev_dbg(&proc->pdev->dev, "Processor %u_%u PD is removed.\n",
		proc->system->system_id, proc->processor_id);
}

/**
 * snps_accel_npx_core_group_pd_probe - Probe a NPX group's PM domain.
 * @parent: Parent generic PM domain.
 * @group_id: Target NPX group ID.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_npx_core_group_pd_probe(struct snps_accel_processor *proc,
				   u32 group_id)
{
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_core_group *group = &npx->groups[group_id];
	struct snps_accel_core *core;
	int ret, i, j;

	group->proc = proc;
	group->group_id = group_id;

	group->genpd.name = devm_kasprintf(&proc->pdev->dev, GFP_KERNEL,
					   "snps_accel_core_group_%u_%u_%u_pd",
					   proc->system->system_id,
					   proc->processor_id, group_id);
	group->genpd.power_off = snps_accel_npx_core_group_power_off;
	group->genpd.power_on = snps_accel_npx_core_group_power_on;
	ret = pm_genpd_init(&group->genpd, NULL, true);
	if (ret) {
		dev_err(&proc->pdev->dev, "Failed to init domain %s: %d\n",
			group->genpd.name, ret);
		return ret;
	}

	for (i = 0; i < npx->num_l2_cores; i++) {
		core = npx->l2_cores[i];
		if (core->unused)
			continue;

		ret = pm_genpd_add_subdomain(&core->genpd, &group->genpd);
		if (ret) {
			dev_err(&proc->pdev->dev,
				"Failed to add %s to parent %s: %d\n",
				group->genpd.name, core->genpd.name, ret);
			goto out_add;
		}
	}

	dev_dbg(&proc->pdev->dev, "NPX core group %u_%u_%u PD is probed.\n",
		proc->system->system_id, proc->processor_id, group_id);

	return 0;

out_add:
	if (i > 0) {
		for (j = i - 1; j >= 0; j--) {
			core = npx->l2_cores[j];
			if (core->unused)
				continue;

			pm_genpd_remove_subdomain(&core->genpd, &group->genpd);
		}
	}

	pm_genpd_remove(&group->genpd);
	return ret;
}

/**
 * snps_accel_npx_group_pd_remove - Remove a NPX group's PM domain.
 * @group: Target NPX group.
 */
static void snps_accel_npx_group_pd_remove(struct snps_accel_core_group *group)
{
	struct snps_accel_processor *proc = group->proc;
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_core *core;
	int i;

	for (i = 0; i < npx->num_l2_cores; i++) {
		core = npx->l2_cores[i];
		if (core->unused)
			continue;

		pm_genpd_remove_subdomain(&core->genpd, &group->genpd);
	}

	pm_genpd_remove(&group->genpd);

	dev_dbg(&proc->pdev->dev, "NPX core group %u_%u_%u PD is removed.\n",
		proc->system->system_id, proc->processor_id, group->group_id);
}

/**
 * snps_accel_npx_pd_probe - Probe a NPX processor's PM domains.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_npx_pd_probe(struct snps_accel_processor *proc)
{
	struct snps_accel_npx *npx = proc->priv;
	struct device_node *node = proc->pdev->dev.of_node;
	struct snps_accel_core_group *group;
	struct snps_accel_core *core;
	struct generic_pm_domain *mgr_genpd;
	int ret, i, j;
	u32 core_id, num_l1_cores_pg;
	bool core_probed[SNPS_ACCEL_NPX_MAX_CORES] = {false};
	bool group_probed[SNPS_ACCEL_NPX_MAX_GROUPS] = {false};

	ret = of_property_read_u32(node, "snps,num-l2-cores",
				   &npx->num_l2_cores);
	if (ret || (proc->num_cores == 1)) {
		if (proc->num_cores == 1) {
			npx->num_l1_cores = proc->num_cores;
			npx->num_l2_cores = 0;
		} else if (proc->num_cores > 1 && proc->num_cores < 10) {
			npx->num_l1_cores = proc->num_cores - 1;
			npx->num_l2_cores = 1;
		} else {
			npx->num_l1_cores = proc->num_cores - 2;
			npx->num_l2_cores = 2;
		}
	} else {
		if ((npx->num_l2_cores == 0) || (npx->num_l2_cores > 2))
			return -EINVAL;

		if ((proc->num_cores >= 8 && npx->num_l2_cores < 2) ||
		    (npx->num_l2_cores >= proc->num_cores))
			return -EINVAL;

		npx->num_l1_cores = proc->num_cores - npx->num_l2_cores;
	}

	ret = of_property_read_u32(node, "snps,num-l1-cores-pg",
				   &num_l1_cores_pg);
	if (ret || (proc->num_cores == 1)) {
		if (npx->num_l1_cores <= 4)
			npx->num_groups = 1;
		else if (npx->num_l1_cores > 4 && npx->num_l1_cores <= 8)
			npx->num_groups = 2;
		else
			npx->num_groups = 4;

		num_l1_cores_pg = npx->num_l1_cores / npx->num_groups;
	} else {
		if (num_l1_cores_pg == 0)
			return -EINVAL;

		if ((npx->num_l1_cores % num_l1_cores_pg) != 0)
			return -EINVAL;

		npx->num_groups = npx->num_l1_cores / num_l1_cores_pg;
	}

	/* Add the processor to its manager's PM domain */
	mgr_genpd = proc->mgr->power_always_on ? NULL : &proc->mgr->genpd;
	ret = snps_accel_proc_pd_probe(mgr_genpd, proc,
				       snps_accel_npx_proc_power_off,
				       snps_accel_npx_proc_power_on);
	if (ret)
		return ret;

	if (proc->num_cores == 1)
		return snps_accel_core_pd_probe(&proc->genpd,
					&proc->cores[proc->start_core_id],
					NULL, NULL);

	/* Add L2 core(s) to the processor's PM domain */
	for (i = 0; i < npx->num_l2_cores; i++) {
		core_id = (i == 0) ? 0 : npx->num_l1_cores + 1;
		core = &proc->cores[core_id];
		ret = snps_accel_l2_core_pd_probe(&proc->genpd, core,
						snps_accel_l2_core_power_off,
						snps_accel_l2_core_power_on);
		if (ret)
			goto out_l2;

		npx->l2_cores[i] = &proc->cores[core_id];
		core_probed[core_id] = true;
	}

	/* Add the L1 group(s) to their PM domain */
	for (i = 0; i < npx->num_groups; i++) {
		ret = snps_accel_npx_core_group_pd_probe(proc, i);
		if (ret)
			goto out_group;

		group_probed[i] = true;
	}

	/* Add the L1 core(s) to the L1 group's PM domain */
	for (i = 0; i < npx->num_groups; i++) {
		group = &npx->groups[i];
		for (j = 0; j < num_l1_cores_pg; j++) {
			core_id = i * num_l1_cores_pg + 1 + j;
			core = &proc->cores[core_id];
			ret = snps_accel_core_pd_probe(&group->genpd, core,
						       NULL, NULL);
			if (ret)
				goto out_l1;

			core_probed[core_id] = true;
		}
	}

	dev_info(&proc->pdev->dev, "NPX processor %u_%u PDs are probed.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;

out_l1:
	for (i = 0; i < npx->num_groups; i++) {
		group = &npx->groups[i];
		for (j = 0; j < num_l1_cores_pg; j++) {
			core_id = i * num_l1_cores_pg + 1 + j;
			if (core_probed[core_id]) {
				core = &proc->cores[core_id];
				snps_accel_core_pd_remove(&group->genpd, core);
			}
		}
	}

out_group:
	for (i = 0; i < npx->num_groups; i++) {
		if (group_probed[i]) {
			group = &npx->groups[i];
			snps_accel_npx_group_pd_remove(group);
		}
	}

out_l2:
	for (i = 0; i < npx->num_l2_cores; i++) {
		core_id = (i == 0) ? 0 : npx->num_l1_cores + 1;
		if (core_probed[core_id]) {
			core = &proc->cores[core_id];
			snps_accel_l2_core_pd_remove(&proc->genpd, core);
		}
	}

	snps_accel_proc_pd_remove(mgr_genpd, proc);
	return ret;
}

/**
 * snps_accel_npx_pd_remove - Remove an NPX processor's PM domains.
 * @proc: Target processor.
 */
void snps_accel_npx_pd_remove(struct snps_accel_processor *proc)
{
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_core_group *group;
	struct snps_accel_core *core;
	struct generic_pm_domain *mgr_genpd;
	int i, j;
	u32 core_id, num_l1_cores_pg = npx->num_l1_cores / npx->num_groups;

	for (i = 0; i < npx->num_groups; i++) {
		group = &npx->groups[i];
		for (j = 0; j < num_l1_cores_pg; j++) {
			core_id = i * num_l1_cores_pg + 1 + j;
			core = &proc->cores[core_id];
			snps_accel_core_pd_remove(&group->genpd, core);
		}
	}

	for (i = 0; i < npx->num_groups; i++) {
		group = &npx->groups[i];
		snps_accel_npx_group_pd_remove(group);
	}

	for (i = 0; i < npx->num_l2_cores; i++) {
		core_id = (i == 0) ? 0 : npx->num_l1_cores + 1;
		core = &proc->cores[core_id];
		snps_accel_core_pd_remove(&proc->genpd, core);
	}

	mgr_genpd = proc->mgr->power_always_on ? NULL : &proc->mgr->genpd;
	snps_accel_proc_pd_remove(mgr_genpd, proc);

	dev_info(&proc->pdev->dev, "NPX processor %u_%u PDs are removed.",
		 proc->system->system_id, proc->processor_id);
}

/**
 * snps_accel_vpx_proc_power_on - Power off callback of a VPX processor.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_vpx_proc_power_on(struct generic_pm_domain *genpd)
{
	struct snps_accel_processor *proc =
		container_of(genpd, struct snps_accel_processor, genpd);
	struct snps_accel_core *primary_core = &proc->cores[0];
	int ret;

	/* Deassert reset of VPX block */
	/* Power up VPX block */

	/* Power up primary core */
	ret = snps_accel_core_resume_top(primary_core);
	if (ret)
		return ret;

	snps_accel_core_resume_bottom(primary_core);

	dev_info(&proc->pdev->dev, "VPX processor %u_%u PD is powered on.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;
}

/**
 * snps_accel_vpx_proc_power_off - Power off callback of a VPX processor.
 * @genpd: Target generic PM domain.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_vpx_proc_power_off(struct generic_pm_domain *genpd)
{
	struct snps_accel_processor *proc =
		container_of(genpd, struct snps_accel_processor, genpd);
	struct snps_accel_core *primary_core = &proc->cores[0];
	int ret;

	/* Power down primary core */
	ret = snps_accel_core_suspend_top(primary_core);
	if (ret)
		return ret;

	snps_accel_core_suspend_bottom(primary_core);

	/* Power down VPX block */
	/* Assert reset of VPX block */

	dev_info(&proc->pdev->dev, "VPX processor %u_%u PD is powered off.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;
}

/**
 * snps_accel_vpx_pd_probe - Probe a VPX processor's PM domain.
 * @proc: Target processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_vpx_pd_probe(struct snps_accel_processor *proc)
{
	struct snps_accel_vpx *vpx = proc->priv;
	struct device *dev = &proc->pdev->dev;
	struct device_node *node = dev->of_node;
	struct snps_accel_core *primary_core, *core;
	struct generic_pm_domain *mgr_genpd;
	int ret, i;
	bool core_probed[SNPS_ACCEL_VPX_MAX_CORES] = {false};

	ret = of_property_read_u32(node, "snps,cluster-block-id",
				   &vpx->cluster_block_id);
	if (ret) {
		vpx->cluster_block_id = U32_MAX;
		dev_info(dev, "Assume 'snps,cluster-block-id': <%u>\n",
			 vpx->cluster_block_id);
	}

	ret = of_property_read_u32(node, "snps,csm-block-id",
				   &vpx->csm_block_id);
	if (ret) {
		vpx->csm_block_id = U32_MAX;
		dev_info(dev, "Assume 'snps,csm-block-id': <%u>\n",
			 vpx->csm_block_id);
	}

	/* Add the processor to its manager's PM domain */
	mgr_genpd = proc->mgr->power_always_on ? NULL : &proc->mgr->genpd;
	ret = snps_accel_proc_pd_probe(mgr_genpd, proc,
				       snps_accel_vpx_proc_power_off,
				       snps_accel_vpx_proc_power_on);
	if (ret)
		return ret;

	/* Add core(s) to the processor's PM domain */
	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		ret = snps_accel_core_pd_probe(&proc->genpd, core,
					       NULL, NULL);
		if (ret)
			goto out_core;

		core_probed[i] = true;
	}

	dev_info(dev, "VPX processor %u_%u PDs are probed.\n",
		 proc->system->system_id, proc->processor_id);

	return 0;

out_core:
	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		if (core_probed[i]) {
			core = &proc->cores[i];
			snps_accel_core_pd_remove(&primary_core->genpd, core);
		}
	}

	snps_accel_proc_pd_remove(mgr_genpd, proc);
	return ret;
}

/**
 * snps_accel_vpx_pd_remove - Remove a VPX processor's PM domains.
 * @proc: Target processor.
 */
void snps_accel_vpx_pd_remove(struct snps_accel_processor *proc)
{
	struct snps_accel_core *core;
	struct generic_pm_domain *mgr_genpd;
	int i;

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		snps_accel_core_pd_remove(&proc->genpd, core);
	}

	mgr_genpd = proc->mgr->power_always_on ? NULL : &proc->mgr->genpd;
	snps_accel_proc_pd_remove(mgr_genpd, proc);

	dev_info(&proc->pdev->dev, "VPX processor %u_%u PDs are removed.\n",
		 proc->system->system_id, proc->processor_id);
}
