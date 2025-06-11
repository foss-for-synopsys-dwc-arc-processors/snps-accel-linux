// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/sysfs.h>
#include <linux/kobject.h>

#include "snps_accel_drv.h"

/*
 * A state-to-string lookup table, for exposing a human readable state
 * via sysfs. Always keep in sync with enum rproc_state
 */
static const char * const rproc_state_string[] = {
	[RPROC_OFFLINE]		= "offline",
	[RPROC_SUSPENDED]	= "suspended",
	[RPROC_RUNNING]		= "running",
	[RPROC_CRASHED]		= "crashed",
	[RPROC_DELETED]		= "deleted",
	[RPROC_ATTACHED]	= "attached",
	[RPROC_DETACHED]	= "detached",
	[RPROC_LAST]		= "invalid",
};

/**
 * state_show - Show operation of a remote processor's read-only state.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 *
 * Only expose the read-only state of the remote processor via sysfs.
 *
 * Return: The number of characters successfully written to @buf.
 */
static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rproc *rproc = container_of(dev, struct rproc, dev);
	unsigned int state;

	state = rproc->state > RPROC_LAST ? RPROC_LAST : rproc->state;
	return sprintf(buf, "%s\n", rproc_state_string[state]);
}
static DEVICE_ATTR_RO(state);

/**
 * snps_accel_ro_rproc_state_create - Create a read-only state attribute.
 * @rproc: Target remote procesor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_ro_rproc_state_create(struct rproc *rproc)
{
	struct snps_accel_rproc_priv *priv = rproc->priv;
	struct snps_accel_processor *proc = priv->proc;
	struct kobject *kobj = &rproc->dev.kobj;
	struct kernfs_node *kn;
	int ret;

	kn = kernfs_find_and_get_ns(kobj->sd, "state", NULL);
	if (!kn) {
		dev_err(&proc->pdev->dev,
			"Failed to find original rproc state node: %d\n",
			ret);
		return ret;
	}

	if (!kn->priv) {
		kernfs_put(kn);
		dev_err(&proc->pdev->dev,
			"Failed to find original rproc state: %d\n", ret);
		return -EINVAL;
	}

	sysfs_remove_file(kobj, kn->priv);
	kernfs_put(kn);

	ret = sysfs_create_file(kobj, &dev_attr_state.attr);
	if (ret) {
		dev_err(&proc->pdev->dev,
			"Failed to create read-only rproc state: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * snps_accel_ro_rproc_state_remove - Remove a read-only state attribute.
 * @rproc: Target remote procesor.
 */
void snps_accel_ro_rproc_state_remove(struct rproc *rproc)
{
	sysfs_remove_file(&rproc->dev.kobj, &dev_attr_state.attr);
}

/**
 * snps_accel_proc_state_equal_to - Check if all remote processor(s) of a processor are in a state.
 * @proc: Target procesor.
 * @state: State.
 *
 * Return: %true if all remote processor(s) of a proc are in an @state, otherwise %false.
 */
static bool snps_accel_proc_state_equal_to(struct snps_accel_processor *proc,
					   unsigned int state)
{
	int i;

	if (proc->num_rprocs == 0)
		return true;

	if (proc->num_rprocs == 1)
		return proc->rprocs[0]->state == state;

	for (i = 0; i < proc->num_rprocs; i++) {
		if (proc->rprocs[i]->state != state)
			return false;
	}

	return true;
}

/**
 * proc_state_show - Show operation of a processor's state.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 *
 * Return: The number of characters successfully written to @buf.
 */
static ssize_t proc_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct snps_accel_processor *proc = dev_get_drvdata(dev);
	struct snps_accel_system *system = dev_get_drvdata(dev->parent);
	unsigned int i, state;
	int ret;
	bool is_valid = false;

	ret = down_read_interruptible(&system->procs_lock);
	if (ret)
		return sprintf(buf, "System is busy: %d\n", ret);

	for (i = 0; i < RPROC_LAST; i++) {
		if (snps_accel_proc_state_equal_to(proc, i)) {
			is_valid = true;
			break;
		}
	}

	up_read(&system->procs_lock);

	state = is_valid ? i : RPROC_LAST;
	return sprintf(buf, "%s\n", rproc_state_string[state]);
}

/**
 * proc_state_store - Store operation of a processor's state.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 * @count: The number of characters written to @buf.
 *
 * Return: @count on success and an appropriate error code otherwise.
 */
static ssize_t proc_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct snps_accel_processor *proc = dev_get_drvdata(dev);
	struct snps_accel_system *system = proc->system;
	int ret = 0;

	ret = down_write_killable(&system->procs_lock);
	if (ret)
		return ret;

	if (system->num_procs == 0) {
		up_write(&system->procs_lock);
		return -ENODEV;
	}

	if (sysfs_streq(buf, "boot")) {
		ret = snps_accel_proc_boot(proc);
		if (ret)
			dev_err(dev, "Boot failed: %d\n", ret);
	} else if (sysfs_streq(buf, "shutdown")) {
		ret = snps_accel_proc_shutdown(proc);
		if (ret)
			dev_err(dev, "Shutdown failed: %d\n", ret);
	} else {
		dev_err(dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}

	up_write(&system->procs_lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(proc_state);

/**
 * proc_rpm_ops_store - Store operation of a processor's runtime PM.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 * @count: The number of characters written to @buf.
 *
 * Return: @count on success and an appropriate error code otherwise.
 */
static ssize_t proc_rpm_ops_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct snps_accel_processor *proc = dev_get_drvdata(dev);
	struct snps_accel_system *system = proc->system;
	int ret = 0;

	ret = down_read_interruptible(&system->procs_lock);
	if (ret)
		return ret;

	if (system->num_procs == 0) {
		up_read(&system->procs_lock);
		return -ENODEV;
	}

	if (sysfs_streq(buf, "get")) {
		ret = snps_accel_proc_rpm_get(proc, 0);
		if (ret)
			dev_err(dev, "Runtime PM 'get' failed: %d\n", ret);
	} else if (sysfs_streq(buf, "put")) {
		ret = snps_accel_proc_rpm_put(proc, RPM_ASYNC | RPM_AUTO);
		if (ret)
			dev_err(dev, "Runtime PM 'put' failed: %d\n", ret);
	} else {
		dev_err(dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}

	up_read(&system->procs_lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(proc_rpm_ops);

#define SNPS_ACCEL_GENPD_LATENCY_MAX_DEPTH	4

struct snps_accel_genpd_latency {
	s64 power_down[SNPS_ACCEL_GENPD_LATENCY_MAX_DEPTH];
	s64 power_up[SNPS_ACCEL_GENPD_LATENCY_MAX_DEPTH];
	const char *name[SNPS_ACCEL_GENPD_LATENCY_MAX_DEPTH];
	u32 num_depth;
};

/**
 * snps_accel_get_genpd_rpm_latency - Get latency of a given PM domain and its parents.
 * @genpd: Target PM domain.
 * @depth: Nesting count for lockdep.
 * @latency: Output latency.
 */
static void
snps_accel_get_genpd_rpm_latency(struct generic_pm_domain *genpd,
				 unsigned int depth,
				 struct snps_accel_genpd_latency *latency)
{
	struct gpd_link *link;
	int i;

	if (depth >= SNPS_ACCEL_GENPD_LATENCY_MAX_DEPTH)
		return;

	list_for_each_entry(link, &genpd->child_links, child_node) {
		struct generic_pm_domain *parent = link->parent;

		if (parent->flags & GENPD_FLAG_IRQ_SAFE) {
			unsigned long flags;

			spin_lock_irqsave_nested(&parent->slock, flags,
						 depth + 1);
			parent->lock_flags = flags;
		} else
			mutex_lock_nested(&parent->mlock, depth + 1);

		latency->name[depth] = parent->name;
		if (!parent->states || parent->state_count == 0) {
			latency->power_down[depth] = -1;
			latency->power_up[depth] = -1;
		} else {
			for (i = 0; i < parent->state_count; i++) {
				struct genpd_power_state *state =
							&parent->states[i];

				latency->power_down[depth] =
						state->power_off_latency_ns;
				latency->power_up[depth] =
						state->power_on_latency_ns;
			}
		}
		latency->num_depth++;

		snps_accel_get_genpd_rpm_latency(parent, depth + 1, latency);

		if (parent->flags & GENPD_FLAG_IRQ_SAFE)
			spin_unlock_irqrestore(&parent->slock,
					       parent->lock_flags);
		else
			mutex_unlock(&parent->mlock);
	}
}

/**
 * proc_rpm_latency_show - Show latency of power management.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 *
 * Return: The number of characters successfully written to @buf.
 */
static ssize_t proc_rpm_latency_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct snps_accel_processor *proc = dev_get_drvdata(dev);
	struct snps_accel_system *system = dev_get_drvdata(dev->parent);
	struct snps_accel_core *core;
	struct device *core_dev;
	struct gpd_timing_data *td;
	struct generic_pm_domain *genpd;
	int i, j, ret;
	ssize_t len = 0;
	struct snps_accel_genpd_latency latency;
	s64 total_power_down_latency_ns, total_power_up_latency_ns;

	latency.num_depth = 0;

	ret = down_read_interruptible(&system->procs_lock);
	if (ret)
		return sprintf(buf, "System %u is busy: %d\n", ret,
			       system->system_id);

	if (system->num_procs == 0) {
		up_read(&system->procs_lock);
		return sprintf(buf, "System %u is empty\n",
			       system->system_id);
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (core->unused)
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		total_power_down_latency_ns = 0;
		total_power_up_latency_ns = 0;
		core_dev = &core->pdev->dev;
		td = &dev_gpd_data(core_dev)->td;

		for (j = 0; j < 4; j++)
			len += sprintf(buf + len, "--------------------");

		len += sprintf(buf + len, "\n");
		len += sprintf(buf + len,
		"snps_accel_core_%u_%u_%u_pd:\tpower_down_latency_ns\t%lld\n",
		system->system_id, proc->processor_id, core->core_id,
		td->suspend_latency_ns);
		len += sprintf(buf + len,
		"snps_accel_core_%u_%u_%u_pd:\tpower_up_latency_ns\t%lld\n",
		system->system_id, proc->processor_id, core->core_id,
		td->resume_latency_ns);
		len += sprintf(buf + len, "\n");

		total_power_down_latency_ns += td->suspend_latency_ns;
		total_power_up_latency_ns += td->resume_latency_ns;

		genpd = pd_to_genpd(core_dev->pm_domain);
		if (genpd->flags & GENPD_FLAG_IRQ_SAFE) {
			unsigned long flags;

			spin_lock_irqsave(&genpd->slock, flags);
			genpd->lock_flags = flags;
		} else
			mutex_lock(&genpd->mlock);

		snps_accel_get_genpd_rpm_latency(genpd, 0, &latency);

		if (genpd->flags & GENPD_FLAG_IRQ_SAFE)
			spin_unlock_irqrestore(&genpd->slock,
					       genpd->lock_flags);
		else
			mutex_unlock(&genpd->mlock);

		for (j = 0; j < latency.num_depth; j++) {
			len += sprintf(buf + len,
				       "%s:\tpower_down_latency_ns\t%lld\n",
				       latency.name[j], latency.power_down[j]);
			len += sprintf(buf + len,
				       "%s:\tpower_up_latency_ns\t%lld\n",
				       latency.name[j], latency.power_up[j]);
			len += sprintf(buf + len, "\n");
			total_power_down_latency_ns += latency.power_down[j];
			total_power_up_latency_ns += latency.power_up[j];
		}

		len += sprintf(buf + len,
			"Core %u_%u_%u:\ttotal_power_down_latency_ns\t%lld\n",
			system->system_id, proc->processor_id,
			core->core_id, total_power_down_latency_ns);
		len += sprintf(buf + len,
			"Core %u_%u_%u:\ttotal_power_up_latency_ns\t%lld\n",
			system->system_id, proc->processor_id,
			core->core_id, total_power_up_latency_ns);
	}

	up_read(&system->procs_lock);

	return len;
}

/**
 * snps_accel_clear_genpd_rpm_latency - Clear latency of a given PM domain and its parents.
 * @genpd: Target PM domain.
 * @depth: Nesting count for lockdep.
 */
static void snps_accel_clear_genpd_rpm_latency(struct generic_pm_domain *genpd,
					       unsigned int depth)
{
	struct gpd_link *link;
	int i;

	list_for_each_entry(link, &genpd->child_links, child_node) {
		struct generic_pm_domain *parent = link->parent;

		if (parent->flags & GENPD_FLAG_IRQ_SAFE) {
			unsigned long flags;

			spin_lock_irqsave_nested(&parent->slock, flags,
						 depth + 1);
			parent->lock_flags = flags;
		} else
			mutex_lock_nested(&parent->mlock, depth + 1);

		for (i = 0; i < parent->state_count; i++) {
			struct genpd_power_state *state =
						&parent->states[i];
			state->power_off_latency_ns = 0;
			state->power_on_latency_ns = 0;
		}

		snps_accel_clear_genpd_rpm_latency(parent, depth + 1);

		if (parent->flags & GENPD_FLAG_IRQ_SAFE)
			spin_unlock_irqrestore(&parent->slock,
					       parent->lock_flags);
		else
			mutex_unlock(&parent->mlock);
	}
}

/**
 * snps_accel_clear_core_rpm_latency - Clean latency of a given core and its parents power domains.
 * @core: Target core.
 */
void snps_accel_clear_core_rpm_latency(struct snps_accel_core *core)
{
	struct device *dev = &core->pdev->dev;
	struct gpd_timing_data *td = &dev_gpd_data(dev)->td;
	struct generic_pm_domain *genpd;

	td->suspend_latency_ns = 0;
	td->resume_latency_ns = 0;

	genpd = pd_to_genpd(dev->pm_domain);
	if (genpd->flags & GENPD_FLAG_IRQ_SAFE) {
		unsigned long flags;

		spin_lock_irqsave(&genpd->slock, flags);
		genpd->lock_flags = flags;
	} else
		mutex_lock(&genpd->mlock);

	snps_accel_clear_genpd_rpm_latency(genpd, 0);

	if (genpd->flags & GENPD_FLAG_IRQ_SAFE)
		spin_unlock_irqrestore(&genpd->slock,
					genpd->lock_flags);
	else
		mutex_unlock(&genpd->mlock);
}

/**
 * proc_rpm_latency_store - Clean latency of power management.
 * @dev: Target device.
 * @attr: Target device attribute.
 * @buf: Output string buffer to show.
 * @count: The number of characters written to @buf.
 *
 * Return: @count on success and an appropriate error code otherwise.
 */
static ssize_t proc_rpm_latency_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct snps_accel_processor *proc = dev_get_drvdata(dev);
	struct snps_accel_system *system = proc->system;
	struct snps_accel_core *core;
	int i, ret = 0;

	ret = down_read_interruptible(&system->procs_lock);
	if (ret)
		return ret;

	if (system->num_procs == 0) {
		up_read(&system->procs_lock);
		return -ENODEV;
	}

	if (!sysfs_streq(buf, "clear")) {
		dev_err(dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}

	for (i = proc->start_core_id;
	     i < proc->start_core_id + proc->num_cores; i++) {
		core = &proc->cores[i];
		if (core->unused)
			continue;

		if (snps_accel_npx_l2_core(core))
			continue;

		snps_accel_clear_core_rpm_latency(core);
	}

	up_read(&system->procs_lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(proc_rpm_latency);

/**
 * snps_accel_proc_sysfs_create - Create sysfs attributes of a processor.
 * @proc: Target procesor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_proc_sysfs_create(struct snps_accel_processor *proc)
{
	int ret;

	ret = sysfs_create_file(&proc->pdev->dev.kobj,
				&dev_attr_proc_state.attr);
	if (ret) {
		dev_err(&proc->pdev->dev,
			"Failed to create processor state attr: %d\n",
			ret);
		return ret;
	}

	ret = sysfs_create_file(&proc->pdev->dev.kobj,
				&dev_attr_proc_rpm_ops.attr);
	if (ret) {
		dev_err(&proc->pdev->dev,
			"Failed to create processor RPM-operations attr: %d\n",
			ret);
		goto err_rpm;
	}

	ret = sysfs_create_file(&proc->pdev->dev.kobj,
				&dev_attr_proc_rpm_latency.attr);
	if (ret) {
		dev_err(&proc->pdev->dev,
			"Failed to create processor RPM-latency attr: %d\n",
			ret);
		goto err_latency;
	}

	return 0;

err_latency:
	sysfs_remove_file(&proc->pdev->dev.kobj,
			  &dev_attr_proc_rpm_ops.attr);

err_rpm:
	sysfs_remove_file(&proc->pdev->dev.kobj,
			  &dev_attr_proc_state.attr);

	return ret;
}

/**
 * snps_accel_proc_sysfs_remove - Remove sysfs attribute of a processor.
 * @proc: Target procesor.
 */
void snps_accel_proc_sysfs_remove(struct snps_accel_processor *proc)
{
	sysfs_remove_file(&proc->pdev->dev.kobj,
			  &dev_attr_proc_rpm_latency.attr);
	sysfs_remove_file(&proc->pdev->dev.kobj, &dev_attr_proc_rpm_ops.attr);
	sysfs_remove_file(&proc->pdev->dev.kobj, &dev_attr_proc_state.attr);
}
