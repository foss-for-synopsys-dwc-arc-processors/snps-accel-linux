// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <uapi/misc/snps_accel.h>

#include "snps_accel_drv.h"

#define SNPS_ACCEL_MAX_MINORS		32
#define SNPS_ACCEL_DRIVER_NAME		"snps_accel"
#define SNPS_ACCEL_DRIVER_VERSION	"2.0"
#define SNPS_ACCEL_CTRL_NAME_FORMAT	"snps_accel_system%u_ctrl"

struct class *snps_accel_class;
static unsigned int snps_accel_major;
static unsigned int snps_accel_minor;

/**
 * snps_accel_parse_ranges - Parse ranges relates to a target node.
 * @dev: Target device.
 * @offset: Output offset between target device node between its ancestor(s).
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_parse_ranges(struct device *dev, off_t *offset)
{
	struct device_node *parent_node, *child_node;
	const __be32 *prop, *ranges_prop;
	int ranges_len, parent_cells, child_cells, size_cells, tuple_len;
	phys_addr_t parent_phys_addr;
	u64 child_addr;

	if (!dev || !offset)
		return -EINVAL;

	*offset = 0;
	child_node = dev->of_node;

	do {
		parent_node = child_node->parent;
		if (!parent_node)
			break;

		ranges_len = 0;
		ranges_prop = of_get_property(parent_node, "ranges", &ranges_len);
		if (ranges_prop && ranges_len > 0) {
			parent_cells = of_n_addr_cells(parent_node);

			prop = of_get_property(parent_node, "#address-cells", NULL);
			if (prop)
				child_cells = be32_to_cpup(prop);
			else
				child_cells = parent_cells;

			prop = of_get_property(parent_node, "#size-cells", NULL);
			if (prop)
				size_cells = be32_to_cpup(prop);
			else
				size_cells = of_n_size_cells(parent_node);

			tuple_len = (child_cells + parent_cells + size_cells) *
				    sizeof(__be32);
			if ((ranges_len % tuple_len) != 0) {
				dev_err(dev, "Incorrect ranges property '%pOFn'\n",
					parent_node);
				return -EINVAL;
			}

			child_addr = of_read_number(ranges_prop, child_cells);
			parent_phys_addr = of_read_number(ranges_prop + child_cells,
							  parent_cells);

			*offset += (child_addr - parent_phys_addr);
		}

		child_node = parent_node;
	} while (true);

	return 0;
}

/**
 * snps_accel_system_release - Release a system.
 * @kref: Reference counter of a system.
 */
void snps_accel_system_release(struct kref *kref)
{
	struct snps_accel_system *system =
		container_of(kref, struct snps_accel_system, refcount);
	kfree(system);
}

/**
 * snps_accel_system_mgr_irq_handler - Mananger IRQ Handler regiestered by a system.
 * @index: Index in all mananger's IRQs starting from 0 as per real number of manager's IRQs.
 * @irq_id: ID in the interrupt vector table of host processor.
 * @processor_id: Sender processor ID.
 * @core_id: Sender core ID.
 * @data: Private data.
 *
 * Return: IRQ_HANDLED.
 */
static irqreturn_t
snps_accel_system_mgr_irq_handler(u32 index, int irq_id,
				  u32 processor_id, u32 core_id, void *data)
{
	struct snps_accel_system *system = data;

	atomic_inc(&system->irq_counts[index]);
	wake_up_interruptible(&system->irq_waitq);

	return IRQ_HANDLED;
}

/**
 * snps_accel_get_device - Get device struct by compatible and id.
 * @compatible: Compatible string of a device node.
 * @id_propname: Name of ID property.
 * @id: Value of ID property.
 *
 * Return: Device struct on success, otherwise an appropriate failure.
 */
struct device *snps_accel_get_device(const char *compatible,
				     const char *id_propname, u32 id)
{
	struct device_node *node;
	struct platform_device *pdev;
	u32 id_value;
	int ret;
	bool found = false;

	if (!compatible || !id_propname) {
		pr_err("%s: Invalid compatible or id_propname.\n",
		       __func__);
		return ERR_PTR(-EINVAL);
	}

	for_each_compatible_node(node, NULL, compatible) {
		ret = of_property_read_u32(node, id_propname, &id_value);
		if (ret)
			continue;

		if (id_value == id) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_err("%s: Failed to find device %u.\n", __func__, id);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(node);
	if (!pdev) {
		of_node_put(node);
		pr_err("%s: Failed to find device node.\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	if (!platform_get_drvdata(pdev)) {
		put_device(&pdev->dev);
		of_node_put(node);
		pr_err("%s: Failed to find driver data.\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	return &pdev->dev;
}

/**
 * snps_accel_put_device - Put device.
 * @dev: Device struct.
 *
 * Return: Device struct on success, otherwise an appropriate failure.
 */
void snps_accel_put_device(struct device *dev)
{
	struct device_node *node = dev->of_node;

	put_device(dev);
	of_node_put(node);
}

/**
 * snps_accel_ctrl_priv_release - Release private data of control device file.
 * @ref: Reference counter of a private data.
 */
static void snps_accel_ctrl_priv_release(struct kref *ref)
{
	struct snps_accel_ctrl_priv *priv =
		container_of(ref, struct snps_accel_ctrl_priv, ref);
	kfree(priv);
}

/**
 * snps_accel_ctrl_priv_get - Get private data of control device file.
 * @priv: Private data of control device file.
 */
void snps_accel_ctrl_priv_get(struct snps_accel_ctrl_priv *priv)
{
	kref_get(&priv->ref);
}

/**
 * snps_accel_ctrl_priv_put - Put private data of control device file.
 * @priv: Private data of control device file.
 */
void snps_accel_ctrl_priv_put(struct snps_accel_ctrl_priv *priv)
{
	kref_put(&priv->ref, snps_accel_ctrl_priv_release);
}

/**
 * snps_accel_ctrl_open - Open operation of control device file.
 * @inode: Index node in VFS.
 * @file: An opened file.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_open(struct inode *inode, struct file *file)
{
	struct cdev *cdev = inode->i_cdev;
	struct snps_accel_ctrl *ctrl =
		container_of(cdev, struct snps_accel_ctrl, cdev);
	struct snps_accel_system *system =
		container_of(ctrl, struct snps_accel_system, ctrl);
	struct snps_accel_ctrl_priv *priv;
	int i, open_count;

	if (!kref_get_unless_zero(&system->refcount)) {
		pr_err("%s: Attempting to open a stale system control.\n",
		       __func__);
		return -ESTALE;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		kref_put(&system->refcount, snps_accel_system_release);
		return -ENOMEM;
	}

	open_count = atomic_inc_return(&ctrl->open_count);
	kref_init(&priv->ref);
	snps_accel_mem_init(system->dev, &priv->mem);

	for (i = 0; i < SNPS_ACCEL_MAX_MGR_IRQS; i++)
		priv->handled_irq_counts[i] = atomic_read(&system->irq_counts[i]);

	priv->system = system;
	file->private_data = priv;

	dev_info(system->dev,
		 "Control is opened. Known open count %d\n", open_count);

	return 0;
}

/**
 * snps_accel_ctrl_release - Release operation of control device file.
 * @inode: Index node in VFS.
 * @file: An opened file.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_release(struct inode *inode, struct file *file)
{
	struct snps_accel_ctrl_priv *priv = file->private_data;
	struct snps_accel_system *system = priv->system;
	int open_count;

	flush_delayed_fput();
	snps_accel_release_import(&priv->mem);
	snps_accel_ctrl_priv_put(priv);
	file->private_data = NULL;
	open_count = atomic_dec_return(&system->ctrl.open_count);
#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	wake_up_all(&system->ctrl.open_count_wq);
#endif
	dev_info(system->dev,
		 "Control is released. Known open count %d\n", open_count);
	kref_put(&system->refcount, snps_accel_system_release);

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_info_shmem - Get base and size of host-device shared memory.
 * @system: Index node in VFS.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_info_shmem(struct snps_accel_system *system,
				 char __user *arg)
{
	struct snps_accel_shmem data;

	data.offset = system->host_shrmem_pbase;
	data.size = system->host_shrmem_size;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_info_notify - Get base and size of manager's register.
 * @system: Index node in VFS.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_info_notify(struct snps_accel_system *system,
				  char __user *arg)
{
	struct snps_accel_notify data;

	data.offset = system->mgr_reg_pbase;
	data.size = system->mgr_reg_size;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * __snps_accel_ctrl_ioctl_collect_mgr_irq - Collect count of a manager interrrupt.
 * @priv: Private data of control device file.
 * @index: Index of manager interrupt(s).
 * @timeout: Timeout waiting for IRQs.
 * @count: Output count of IRQs.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
__snps_accel_ctrl_ioctl_collect_mgr_irq(struct snps_accel_ctrl_priv *priv,
					u32 index, u32 timeout, u32 *count)
{
	struct snps_accel_system *system = priv->system;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&system->irq_waitq, &wait);
	*count = atomic_read(&system->irq_counts[index]);
	if (timeout == 0)
		goto out;

	if (priv->handled_irq_counts[index] != *count)
		goto out;

	set_current_state(TASK_INTERRUPTIBLE);
	if (schedule_timeout(msecs_to_jiffies(timeout)) == 0)
		ret = -ETIMEDOUT;

	__set_current_state(TASK_RUNNING);
	*count = atomic_read(&system->irq_counts[index]);

out:
	remove_wait_queue(&system->irq_waitq, &wait);
	priv->handled_irq_counts[index] = *count;

	return ret;
}

/**
 * snps_accel_ctrl_ioctl_wait_mgr_irq - Collect count of a manager interrrupt.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_wait_mgr_irq(struct snps_accel_ctrl_priv *priv,
				   char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_wait_irq data;
	int ret;
	u32 count;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	ret = __snps_accel_ctrl_ioctl_collect_mgr_irq(priv, 0, data.timeout,
						      &count);
	data.count = count;

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return ret;
}

/**
 * snps_accel_ctrl_ioctl_dmabuf_alloc - Allocate DMA buffer.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_dmabuf_alloc(struct snps_accel_ctrl_priv *priv,
				   char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_dmabuf_alloc data;
	struct snps_accel_mem_buffer *mbuf = NULL;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	mbuf = snps_accel_dmabuf_create(&priv->mem, data.size, data.flags);
	if (!mbuf) {
		dev_err(system->dev, "Failed to create DMA buffer\n");
		return -ENOMEM;
	}

	data.fd = mbuf->fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		snps_accel_dmabuf_release(mbuf);
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_dmabuf_info - Get information of DMA buffer.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_ioctl_dmabuf_info(struct snps_accel_ctrl_priv *priv,
					     char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_dmabuf_info data;
	int ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	ret = snps_accel_get_dmabuf_info(&priv->mem, &data);
	if (ret) {
		dev_err(system->dev,
			"Failed to get info of DMA buffer: %d\n", ret);
		return ret;
	}

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_dmabuf_import - Import DMA buffer.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_dmabuf_import(struct snps_accel_ctrl_priv *priv,
				    char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_dmabuf_import data;
	int ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	ret = snps_accel_do_dmabuf_import(&priv->mem, data.fd);
	if (ret) {
		dev_err(system->dev,
			"Failed to import DMA buffer: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_dmabuf_detach - Detach DMA buffer.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_dmabuf_detach(struct snps_accel_ctrl_priv *priv,
				    char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_dmabuf_detach data;
	int ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	ret = snps_accel_do_dmabuf_detach(&priv->mem, data.fd);
	if (ret) {
		dev_err(system->dev,
			"Failed to detach DMA buffer: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * snps_accel_ctrl_ioctl_collect_mgr_irq - Collect count of a manager interrrupt.
 * @priv: Private data of control device file.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_collect_mgr_irq(struct snps_accel_ctrl_priv *priv,
				      char __user *arg)
{
	struct snps_accel_system *system = priv->system;
	struct snps_accel_collect_mgr_irq data;
	int ret;
	u32 count;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	ret = __snps_accel_ctrl_ioctl_collect_mgr_irq(priv, data.index,
						      data.timeout_ms,
						      &count);
	data.count = count;

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		return -EFAULT;
	}

	return ret;
}

/**
 * snps_accel_ctrl_ioctl_proc_cmd - Processor command.
 * @system: Accelerator system.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_proc_cmd(struct snps_accel_system *system,
			       char __user *arg)
{
	struct snps_accel_ioctl_proc_cmd data;
	struct snps_accel_processor *proc;
	int i, ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	if (data.num_procs > SNPS_ACCEL_IOCTL_MAX_RPOC_CMDS ||
	    data.cmd_id >= SNPS_ACCEL_IOCTL_PROC_CMD_NUM) {
		dev_err(system->dev,
			"Invalid command: num %u cmd_id %u\n",
			data.num_procs, data.cmd_id);
		return -EINVAL;
	}

	if (data.num_procs == 0) {
		dev_info(system->dev,
			 "No processor command to be processed\n");
		return 0;
	}

	ret = down_write_killable(&system->procs_lock);
	if (ret) {
		dev_err(system->dev, "Failed to lock system: %d\n", ret);
		return ret;
	}

	if (system->num_procs == 0) {
		up_write(&system->procs_lock);
		ret = -ENODEV;
		dev_err(system->dev,
			"There is not any processor in system\n");
		goto out;
	}

	for (i = 0; i < data.num_procs; i++) {
		if (data.procs[i].processor_id > SNPS_ACCEL_MAX_PROCESSORS_PS) {
			data.procs[i].result = -EINVAL;
			ret = -EINVAL;
			continue;
		}

		proc = system->procs[data.procs[i].processor_id];
		if (!proc) {
			data.procs[i].result = -ENODEV;
			continue;
		}

		dev_dbg(system->ctrl.cdev_dev,
			"%s: Processing command %u to processor %u_%u.\n",
			__func__, data.cmd_id, system->system_id,
			proc->processor_id);

		switch (data.cmd_id) {
		case SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_START:
			data.procs[i].result = snps_accel_proc_boot(proc);
			break;
		case SNPS_ACCEL_IOCTL_PROC_CMD_BOOT_END:
		case SNPS_ACCEL_IOCTL_PROC_CMD_RPM_PUT:
			data.procs[i].result =
				snps_accel_proc_rpm_put(proc,
							RPM_ASYNC | RPM_AUTO);
			break;
		case SNPS_ACCEL_IOCTL_PROC_CMD_SHUTDOWN:
			data.procs[i].result = snps_accel_proc_shutdown(proc);
			break;
		case SNPS_ACCEL_IOCTL_PROC_CMD_RPM_GET:
			data.procs[i].result =
				snps_accel_proc_rpm_get(proc, 0);
			break;
		default:
			data.procs[i].result = -ENOTSUPP;
			break;
		}

		if (data.procs[i].result)
			ret = data.procs[i].result;
	}

out:
	up_write(&system->procs_lock);

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		ret = -EFAULT;
	}

	return ret;
}

/**
 * snps_accel_ctrl_ioctl_rpm_get - Get runtime PM.
 * @system: Accelerator system.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_ctrl_ioctl_rpm_get(struct snps_accel_system *system,
			      char __user *arg)
{
	struct snps_accel_ioctl_rpm_cmd data;
	int ret;
	bool cores_wait[SNPS_ACCEL_IOCTL_MAX_RPM_CORES] = {false};

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	if (unlikely(data.num_cores > SNPS_ACCEL_IOCTL_MAX_RPM_CORES)) {
		dev_err(system->dev,
			"Invalid command: num %u\n", data.num_cores);
		return -EINVAL;
	}

	if (data.num_cores == 0) {
		dev_info(system->dev,
			 "No processor core command to be processed\n");
		return 0;
	}

	ret = down_read_interruptible(&system->procs_lock);
	if (unlikely(ret)) {
		dev_err(system->dev, "Failed to lock system: %d\n", ret);
		return ret;
	}

	if (unlikely(system->num_procs == 0)) {
		up_read(&system->procs_lock);
		ret = -ENODEV;
		dev_err(system->dev,
			"There is not any processor in system\n");
		return ret;
	}

	ret = snps_accel_cores_rpm_get(system, &data, cores_wait,
				       SNPS_ACCEL_IOCTL_MAX_RPM_CORES,
				       RPM_ASYNC);
	up_read(&system->procs_lock);
	if (ret) {
		dev_err(system->dev,
			"Failed to get runtime PM usage: %d\n", ret);
		return ret;
	}

	snps_accel_cores_rpm_wait(system, &data, cores_wait,
				  SNPS_ACCEL_IOCTL_MAX_RPM_CORES);

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		ret = -EFAULT;
	}

	return ret;
}

/**
 * snps_accel_ctrl_ioctl_rpm_put - Put runtime PM.
 * @system: Accelerator system.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_ioctl_rpm_put(struct snps_accel_system *system,
					 char __user *arg)
{
	struct snps_accel_ioctl_rpm_cmd data;
	int ret;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		dev_err(system->dev, "Failed to copy from user mode.\n");
		return -EFAULT;
	}

	if (unlikely(data.num_cores > SNPS_ACCEL_IOCTL_MAX_RPM_CORES)) {
		dev_err(system->dev,
			"Invalid command: num %u\n", data.num_cores);
		return -EINVAL;
	}

	if (data.num_cores == 0) {
		dev_info(system->dev,
			 "No processor core command to be processed\n");
		return 0;
	}

	ret = down_read_interruptible(&system->procs_lock);
	if (unlikely(ret)) {
		dev_err(system->dev, "Failed to lock system: %d\n", ret);
		return ret;
	}

	if (unlikely(system->num_procs == 0)) {
		up_read(&system->procs_lock);
		ret = -ENODEV;
		dev_err(system->dev,
			"There is not any processor in system\n");
		return ret;
	}

	ret = snps_accel_cores_rpm_put(system, &data, RPM_ASYNC | RPM_AUTO);
	up_read(&system->procs_lock);
	if (ret) {
		dev_err(system->dev,
			"Failed to put runtime PM usage: %d\n", ret);
		return ret;
	}

	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		dev_err(system->dev, "Failed to copy to user mode.\n");
		ret = -EFAULT;
	}

	return ret;
}

/**
 * snps_accel_ctrl_ioctl - IOCTL operaton of system control device file.
 * @file: An opened file.
 * @cmd: IOCTL cmd.
 * @arg: IOCTL argument.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static long snps_accel_ctrl_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct snps_accel_ctrl_priv *priv = file->private_data;
	struct snps_accel_system *system;
	char __user *argp = (char __user *)arg;
	int ret;

	if (unlikely(!priv)) {
		pr_err("%s: Failed to get valid private data", __func__);
		return -EINVAL;
	}

	system = priv->system;
	if (unlikely(!system->ctrl.cdev_dev)) {
		dev_err(system->dev,
			"Failed to get charactor device of control.\n");
		return -ENODEV;
	}

	switch (cmd) {
	case SNPS_ACCEL_IOCTL_INFO_SHMEM:
		ret = snps_accel_ctrl_ioctl_info_shmem(system, argp);
		break;
	case SNPS_ACCEL_IOCTL_INFO_NOTIFY:
		ret = snps_accel_ctrl_ioctl_info_notify(system, argp);
		break;
	case SNPS_ACCEL_IOCTL_WAIT_IRQ:
		ret = snps_accel_ctrl_ioctl_wait_mgr_irq(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_ALLOC:
		ret = snps_accel_ctrl_ioctl_dmabuf_alloc(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_INFO:
		ret = snps_accel_ctrl_ioctl_dmabuf_info(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_IMPORT:
		ret = snps_accel_ctrl_ioctl_dmabuf_import(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_DMABUF_DETACH:
		ret = snps_accel_ctrl_ioctl_dmabuf_detach(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_COLLECT_MGR_IRQ:
		ret = snps_accel_ctrl_ioctl_collect_mgr_irq(priv, argp);
		break;
	case SNPS_ACCEL_IOCTL_PROC_CMD:
		ret = snps_accel_ctrl_ioctl_proc_cmd(system, argp);
		break;
	case SNPS_ACCEL_IOCTL_RPM_GET:
		ret = snps_accel_ctrl_ioctl_rpm_get(system, argp);
		break;
	case SNPS_ACCEL_IOCTL_RPM_PUT:
		ret = snps_accel_ctrl_ioctl_rpm_put(system, argp);
		break;
	default:
		dev_err(system->dev, "Unknown control IOCTL command %x\n",
			cmd);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

/**
 * snps_accel_ctrl_mmap - Memory map operaton of system control device file.
 * @file: An opened file.
 * @vma: Virtual memory area.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct snps_accel_ctrl_priv *priv = file->private_data;
	struct snps_accel_system *system = priv->system;
	struct snps_accel_ctrl *ctrl = &system->ctrl;
	u64 addr = vma->vm_pgoff << PAGE_SHIFT;
	size_t size = vma->vm_end - vma->vm_start;
	int ret;

	dev_dbg(ctrl->cdev_dev,
		"%s: start %#lx end %#lx pgoff %#lx (%pap)\n",
		__func__, vma->vm_start, vma->vm_end, vma->vm_pgoff, &addr);

	if (addr == system->host_shrmem_pbase) {
		if (size != system->host_shrmem_size && size != PAGE_SIZE) {
			dev_err(ctrl->cdev_dev,
				"Invalid shared memory sz (%#zx vs %#zx)\n",
				size, system->host_shrmem_size);
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				      size, vma->vm_page_prot);
	} else if (addr == system->mgr_reg_pbase) {
		if (size != system->mgr_reg_size && size != PAGE_SIZE) {
			dev_err(ctrl->cdev_dev,
				"Invalid notify memory sz (%#zx vs %#zx)\n",
				size, system->mgr_reg_size);
			return -EINVAL;
		}
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		ret = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					 size, vma->vm_page_prot);
	} else {
		dev_err(ctrl->cdev_dev,
			"Unsupported address to mmap %pap\n", &addr);
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations snps_accel_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= snps_accel_ctrl_open,
	.release	= snps_accel_ctrl_release,
	.unlocked_ioctl	= snps_accel_ctrl_ioctl,
	.compat_ioctl	= snps_accel_ctrl_ioctl,
	.mmap		= snps_accel_ctrl_mmap,
};

/**
 * snps_accel_ctrl_init - Initialize system control.
 * @ctrl: system control.
 * @major: Major device number.
 * @minor: Minor device number.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_init(struct snps_accel_ctrl *ctrl, unsigned int major,
				unsigned int minor)
{
	struct snps_accel_system *system =
			container_of(ctrl, struct snps_accel_system, ctrl);
	int ret;

	cdev_init(&ctrl->cdev, &snps_accel_ctrl_fops);
	ctrl->cdev_id = MKDEV(major, minor);
	ctrl->cdev.owner = THIS_MODULE;
	ret = cdev_add(&ctrl->cdev, ctrl->cdev_id, SNPS_ACCEL_MAX_MINORS);
	if (ret) {
		dev_err(system->dev,
			"Failed to add character device: %d\n", ret);
		return ret;
	}

	ctrl->cdev_dev = device_create(snps_accel_class, system->dev,
				       ctrl->cdev_id, ctrl,
				       SNPS_ACCEL_CTRL_NAME_FORMAT,
				       system->system_id);
	if (IS_ERR(ctrl->cdev_dev)) {
		dev_err(system->dev, "Failed to create device /dev/"
			SNPS_ACCEL_CTRL_NAME_FORMAT"\n", system->system_id);
		ret = PTR_ERR(ctrl->cdev_dev);
		goto out_dev_create;
	}

	atomic_set(&ctrl->open_count, 0);
	init_waitqueue_head(&ctrl->open_count_wq);

	return 0;

out_dev_create:
	cdev_del(&ctrl->cdev);

	return ret;
}

/**
 * snps_accel_ctrl_exit - Exit system control.
 * @ctrl: system control.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_ctrl_exit(struct snps_accel_ctrl *ctrl)
{
	int ret;
	struct snps_accel_system *system =
		container_of(ctrl, struct snps_accel_system, ctrl);

	device_destroy(snps_accel_class, ctrl->cdev_id);
	cdev_del(&ctrl->cdev);
	ctrl->cdev_dev = NULL;

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	ret = wait_event_interruptible(ctrl->open_count_wq,
				       atomic_read(&ctrl->open_count) == 0);
	if (ret)
		dev_warn(system->dev,
			 "Removing a system control device with %d opens\n",
			 atomic_read(&ctrl->open_count));
#else
	ret = atomic_read(&ctrl->open_count);
	if (ret)
		dev_warn(system->dev,
			 "Removing a system control device with %d opens\n",
			 ret);
#endif

	return ret;
}

/**
 * snps_accel_verify_lineage - Verify lineage.
 * @pdev: Target platform device.
 * @p_name: Compatible string of parent device node.
 * @gp_name: Compatible string of grandparent device node.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_verify_lineage(struct platform_device *pdev,
			      const char *p_name, const char *gp_name)
{
	struct device *dev = &pdev->dev;
	struct device *p_dev;
	struct device *gp_dev;
	struct device_node *p_node;
	struct device_node *gp_node;

	if (!p_name) {
		dev_err(dev, "Failed to find name of a parent\n");
		return -EINVAL;
	}

	p_node = dev->of_node->parent;
	if (!p_node) {
		dev_err(dev, "Failed to find a parent device node\n");
		return -ENODEV;
	}

	if (!of_device_is_compatible(p_node, p_name)) {
		dev_err(dev, "Found an unexpected parent device node\n");
		return -EINVAL;
	}

	p_dev = dev->parent;
	if (!p_dev) {
		dev_err(dev, "Failed to find a parent device\n");
		return -ENODEV;
	}

	if (!dev_get_drvdata(p_dev)) {
		dev_warn(dev, "The parent device %s is not ready\n",
			 dev_name(p_dev));
		return -EPROBE_DEFER;
	}

	if (gp_name) {
		gp_node = p_node->parent;
		if (!gp_node) {
			dev_err(dev,
				"Failed to find a grandparent device node\n");
			return -ENODEV;
		}

		if (!of_device_is_compatible(gp_node, gp_name)) {
			dev_err(dev,
				"Unexpected grandparent device node\n");
			return -EINVAL;
		}

		gp_dev = p_dev->parent;
		if (!gp_dev) {
			dev_err(dev, "Failed to find a grandparent device\n");
			return -ENODEV;
		}

		if (!dev_get_drvdata(gp_dev)) {
			dev_warn(dev, "Grandparent device %s is not ready\n",
				 dev_name(gp_dev));
			return -EPROBE_DEFER;
		}
	}

	return 0;
}

/**
 * snps_accel_system_probe - Probe a system.
 * @pdev: Target platform device.
 * @data: Match data.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_system_probe(struct platform_device *pdev,
				   const void *data)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct snps_accel_mgr *mgr;
	struct snps_accel_system *system;
	struct resource *res;
	off_t range_offset;
	int ret, i;
	u32 num_mgr_irqs, dma_bits;

	ret = snps_accel_verify_lineage(pdev, SNPS_ACCEL_DRV_MATCH_ARCSYNC,
					NULL);
	if (ret)
		return ret;

	mgr = dev_get_drvdata(dev->parent);

	system = kzalloc(sizeof(*system), GFP_KERNEL);
	if (!system)
		return -ENOMEM;
	dev_set_drvdata(dev, system);

	kref_init(&system->refcount);
	init_rwsem(&system->procs_lock);
	system->data = data;
	system->dev = dev;
	init_waitqueue_head(&system->irq_waitq);
	for (i = 0; i < SNPS_ACCEL_MAX_MGR_IRQS; i++)
		atomic_set(&system->irq_counts[i], 0);
	system->mgr = mgr;
	system->mgr_reg_pbase = mgr->reg_pbase;
	system->mgr_reg_size = mgr->reg_size;

	num_mgr_irqs = of_property_count_u32_elems(node,
						   "snps,mgr-irq-indexes");
	if (num_mgr_irqs > 0) {
		if (!mgr->ops || !mgr->ops->register_irq_handler) {
			dev_err(dev,
				"Manager %u doesn't provide operations\n",
				mgr->mgr_id);
			goto out_no_ops;
		}

		for (i = 0; i < num_mgr_irqs; i++) {
			ret = mgr->ops->register_irq_handler(mgr->dev,
				snps_accel_system_mgr_irq_handler, system, i);
			if (ret) {
				dev_err(dev,
					"Failed to register IRQ %d handler\n",
					i);
				goto out_reg_irq;
			}
		}
	}

	ret = of_property_read_u32(node, "snps,accel-system-id",
				   &system->system_id);
	if (ret) {
		dev_err(dev, "Failed to read 'snps,accel-system-id': %d\n",
			ret);
		goto out_reg_irq;
	}

	ret = snps_accel_parse_ranges(&pdev->dev, &range_offset);
	if (ret) {
		dev_err(dev,
			"Failed to parse 'ranges': %d\n", ret);
		goto out_reg_irq;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "host_shrmem");
	if (!res) {
		dev_err(dev, "Failed to get memory 'host_shrmem'\n");
		ret = -EINVAL;
		goto out_reg_irq;
	}

	system->host_shrmem_pbase = res->start;
	system->host_shrmem_dbase = res->start + range_offset;
	system->host_shrmem_size = resource_size(res);

	dev_info(dev,
		 "Host shared memory: pa %pa da %#llx sz %#zx\n",
		 &system->host_shrmem_pbase, system->host_shrmem_dbase,
		 system->host_shrmem_size);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "dev_shrmem");
	if (!res)
		dev_warn(dev, "Assume 'dev_shrmem' doesn't exist.\n");
	else {
		system->dev_shrmem_dbase = res->start + range_offset;
		system->dev_shrmem_size = resource_size(res);

		dev_info(dev,
			 "Device shared memory: da %#llx sz %#zx\n",
			 system->dev_shrmem_dbase, system->dev_shrmem_size);

		if (of_property_read_bool(node, "snps,zero-dev-shrmem")) {
			void *dev_shrmem_vbase = memremap(res->start,
							  resource_size(res),
							  MEMREMAP_WT);
			if (IS_ERR(dev_shrmem_vbase)) {
				dev_err(dev, "Failed to map 'dev_shrmem' %pap\n",
					&res->start);
				ret = PTR_ERR(dev_shrmem_vbase);
				goto out_reg_irq;
			}

			memset(dev_shrmem_vbase, 0, resource_size(res));
			memunmap(dev_shrmem_vbase);
			dev_info(dev, "Device shared memory is zeroed\n");
		}
	}

	ret = of_property_read_u32(node, "snps,dma-bits", &dma_bits);
	if (ret) {
		dma_bits = 32;
		dev_warn(system->dev,
			 "Assume 'snps,dma-bits': <%u>\n", dma_bits);
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(dma_bits));
	if (ret) {
		dev_err(dev, "Failed to set DMA mask: %d\n", ret);
		goto out_reg_irq;
	}

	dev_info(dev, "dma mask %#llx, coherent %#llx\n",
		 dev->dma_mask ? *dev->dma_mask : 0, dev->coherent_dma_mask);

	ret = dma_set_max_seg_size(dev, 0xfffff);
	if (ret) {
		dev_err(dev, "Failed to set DMA segment size: %d\n", ret);
		goto out_reg_irq;
	}

	ret = snps_accel_ctrl_init(&system->ctrl, snps_accel_major,
				   snps_accel_minor++);
	if (ret)
		goto out_reg_irq;

	dev_info(dev, "System %u is probed.\n", system->system_id);

	return 0;

out_reg_irq:
	for (i = 0; i < num_mgr_irqs; i++)
		mgr->ops->deregister_irq_handler(mgr->dev,
			snps_accel_system_mgr_irq_handler, i);

out_no_ops:
	kref_put(&system->refcount, snps_accel_system_release);
	return ret;
}

/**
 * snps_accel_system_remove - Remove a system.
 * @pdev: Target platform device.
 */
static void snps_accel_system_remove(struct platform_device *pdev)
{
	struct snps_accel_system *system = platform_get_drvdata(pdev);
	struct snps_accel_mgr *mgr = system->mgr;

	snps_accel_ctrl_exit(&system->ctrl);
	if (mgr->ops && mgr->ops->deregister_irq_handler) {
		mgr->ops->deregister_irq_handler(mgr->dev,
			snps_accel_system_mgr_irq_handler, 0);
	}
	system->mgr = NULL;
	dev_set_drvdata(&pdev->dev, NULL);
	kref_put(&system->refcount, snps_accel_system_release);
}

static const struct snps_accel_dev_data snps_accel_arcsync_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_ARCSYNC,
	.probe	= snps_accel_arcsync_probe,
	.remove	= snps_accel_arcsync_remove,
};

static const struct snps_accel_dev_data snps_accel_system_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_SYSTEM,
	.probe	= snps_accel_system_probe,
	.remove	= snps_accel_system_remove,
};

static const struct snps_accel_dev_data snps_accel_npx6_v1_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_NPX6_V1,
	.probe	= snps_accel_npx_probe,
	.remove	= snps_accel_npx_remove,
};

static const struct snps_accel_dev_data snps_accel_npx6_v2_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_NPX6_V2,
	.probe	= snps_accel_npx_probe,
	.remove	= snps_accel_npx_remove,
};

static const struct snps_accel_dev_data snps_accel_npx6_v2p1_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_NPX6_V2p1,
	.probe	= snps_accel_npx_probe,
	.remove	= snps_accel_npx_remove,
};

static const struct snps_accel_dev_data snps_accel_npx6_v2p2_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_NPX6_V2p2,
	.probe	= snps_accel_npx_probe,
	.remove	= snps_accel_npx_remove,
};

static const struct snps_accel_dev_data snps_accel_vpx5_data = {
	.type	= SNPS_ACCEL_DEV_TYPE_VPX5,
	.probe	= snps_accel_vpx_probe,
	.remove	= snps_accel_vpx_remove,
};

static const struct of_device_id snps_accel_match[] = {
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_ARCSYNC,
		.data		= &snps_accel_arcsync_data,
	},
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_SYSTEM,
		.data		= &snps_accel_system_data,
	},
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_NPX6_V1,
		.data		= &snps_accel_npx6_v1_data,
	},
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_NPX6_V2,
		.data		= &snps_accel_npx6_v2_data,
	},
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_NPX6_V2p1,
		.data		= &snps_accel_npx6_v2p1_data,
	},
	{
		.compatible	= SNPS_ACCEL_DRV_MATCH_VPX5,
		.data		= &snps_accel_vpx5_data,
	},
	{
		/* End */
	},
};
MODULE_DEVICE_TABLE(of, snps_accel_match);

static const struct dev_pm_ops snps_accel_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(snps_accel_runtime_suspend,
			   snps_accel_runtime_resume, NULL)
};

/**
 * snps_accel_probe - Probe a driver.
 * @pdev: Target platform device.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct snps_accel_dev_data *data;

	match = of_match_node(snps_accel_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	data = match->data;
	if (data && data->probe)
		return data->probe(pdev, match->data);

	return 0;
}

/**
 * snps_accel_remove - Remove a driver.
 * @pdev: Target platform device.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_remove(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct snps_accel_dev_data *data;

	match = of_match_node(snps_accel_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	data = match->data;
	if (data && data->probe)
		data->remove(pdev);

	return 0;
}

static struct platform_driver snps_accel_driver = {
	.probe = snps_accel_probe,
	.remove = snps_accel_remove,
	.driver = {
		.name = SNPS_ACCEL_DRIVER_NAME,
		.of_match_table = of_match_ptr(snps_accel_match),
		.pm = pm_ptr(&snps_accel_pm_ops),
	},
};

/**
 * snps_accel_init - Initialize a driver module.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int __init snps_accel_init(void)
{
	int ret;
	dev_t dev_id;

	snps_accel_class = class_create(THIS_MODULE, "snps_accel");
	if (IS_ERR(snps_accel_class)) {
		ret = PTR_ERR(snps_accel_class);
		goto out_class;
	}

	ret = alloc_chrdev_region(&dev_id, 0, SNPS_ACCEL_MAX_MINORS,
				  SNPS_ACCEL_DRIVER_NAME);
	if (ret)
		goto out_chr;

	snps_accel_major = MAJOR(dev_id);
	snps_accel_minor = 0;

	ret = platform_driver_register(&snps_accel_driver);
	if (ret < 0)
		goto out_reg;

	pr_info("%s version %s is initialized.\n", SNPS_ACCEL_DRIVER_NAME,
		SNPS_ACCEL_DRIVER_VERSION);

	return ret;

out_reg:
	unregister_chrdev_region(dev_id, SNPS_ACCEL_MAX_MINORS);

out_chr:
	class_destroy(snps_accel_class);

out_class:
	return ret;
}

/**
 * snps_accel_init - Exit a driver module.
 */
static void __exit snps_accel_exit(void)
{
	platform_driver_unregister(&snps_accel_driver);
	unregister_chrdev_region(MKDEV(snps_accel_major, 0),
				 SNPS_ACCEL_MAX_MINORS);
	class_destroy(snps_accel_class);

	pr_info("%s version %s has exited.\n", SNPS_ACCEL_DRIVER_NAME,
		SNPS_ACCEL_DRIVER_VERSION);
}

late_initcall(snps_accel_init);
module_exit(snps_accel_exit);

MODULE_AUTHOR("Synopsys Inc.");
MODULE_DESCRIPTION("Synopsys AI accelerator driver");
MODULE_VERSION(SNPS_ACCEL_DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
