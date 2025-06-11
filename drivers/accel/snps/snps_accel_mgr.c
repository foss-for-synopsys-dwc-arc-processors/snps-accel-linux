// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "snps_accel_drv.h"

#define ARCSYNC_NUM_CORE_0_3(base)		((base) + 0x4)
#define ARCSYNC_NUM_CORE_4_7(base)		((base) + 0x8)
#define ARCSYNC_NUM_CORE_8_11(base)		((base) + 0xc)
#define ARCSYNC_NUM_CORE_12_15(base)		((base) + 0x10)

/* ARCSync v1 */
#define ARCSYNC_V1_PMU_SET_RSTCNT(base)		((base) + 0x80)
#define ARCSYNC_V1_PMU_SET_PUCNT(base)		((base) + 0x84)
#define ARCSYNC_V1_PMU_SET_PDCNT(base)		((base) + 0x88)
#define ARCSYNC_V1_CGATE(base, num_processors, processor_id)	\
	((base) + 256 + 8 * (num_processors) + 4 * (processor_id))
#define ARCSYNC_V1_CORE_RUN_BASE(base)		((base) + 0x1000)
#define ARCSYNC_V1_CORE_RUN(base, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 4 * (as_coreid))
#define ARCSYNC_V1_CORE_HALT(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 4 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_BOOT_IVB_LO(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 8 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_BOOT_IVB_HI(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 12 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_CORE_STATUS(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 16 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_CORE_RESET(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 20 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_CORE_PMODE(base, max_cores, as_coreid)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + 24 * (max_cores) + 4 * (as_coreid))
#define ARCSYNC_V1_EID_RAISE_IRQ(base, max_cores, as_coreid, idx)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + (44 + 8 * (idx)) * (max_cores) + \
	4 * (as_coreid))
#define ARCSYNC_V1_EID_ACK_IRQ(base, max_cores, as_coreid, idx)	\
	(ARCSYNC_V1_CORE_RUN_BASE(base) + (48 + 8 * (idx)) * (max_cores) + \
	4 * (as_coreid))

/* ARCSync v2 */
#define ARCSYNC_V2_CL_ENABLE_BASE(base)		((base) + 0x1000)
#define ARCSYNC_V2_CL_GRP_CLK_EN(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 4 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_CL_GRP_RESET(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 8 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_SET_RSTCNT(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 32 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_SET_PUCNT(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 36 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_SET_PDCNT(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 40 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_CL_GRP_PMODE(base, num_processors, processor_id, group_id) \
	(ARCSYNC_V2_CL_ENABLE_BASE(base) +	\
	(44 + 4 * (group_id)) * (num_processors) + 4 * (processor_id))
#define ARCSYNC_V2_PMU_CORE_LOGIC1(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 60 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_CORE_LOGIC2(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 64 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_GRP_LOGIC1(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 68 * (num_processors) +	\
	4 * (processor_id))
#define ARCSYNC_V2_PMU_GRP_LOGIC2(base, num_processors, processor_id)	\
	(ARCSYNC_V2_CL_ENABLE_BASE(base) + 72 * (num_processors) +	\
	4 * (processor_id))

#define ARCSYNC_V2_VPX_BLOCK_CONTROL(base)	((base) + 0x1c00)
#define ARCSYNC_V2_PMU_VPX_BLOCK_PMODE(base, vpx_block_id)	\
	(ARCSYNC_V2_VPX_BLOCK_CONTROL(base) + 4 * (vpx_block_id))
#define ARCSYNC_V2_VPX_BLOCK_STATUS(base, num_vpx_blocks, vpx_block_id)	\
	(ARCSYNC_V2_VPX_BLOCK_CONTROL(base) + 4 * (num_vpx_blocks) +	\
	4 * (vpx_block_id))
#define ARCSYNC_V2_VPX_BLOCK_RESET(base, num_vpx_blocks, vpx_block_id)	\
	(ARCSYNC_V2_VPX_BLOCK_CONTROL(base) + 8 * (num_vpx_blocks) +	\
	4 * (vpx_block_id))
#define ARCSYNC_V2_VPX_BLOCK_CLK_EN(base, num_vpx_blocks, vpx_block_id)	\
	(ARCSYNC_V2_VPX_BLOCK_CONTROL(base) + 12 * (num_vpx_blocks) +	\
	4 * (vpx_block_id))

#define ARCSYNC_V2_CORE_PMODE_BASE(base)	((base) + 0x2000)
#define ARCSYNC_V2_CORE_PMODE(base, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 4 * (as_coreid))
#define ARCSYNC_V2_CORE_RUN(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 4 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_CORE_HALT(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 8 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_BOOT_IVB_LO(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 12 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_BOOT_IVB_HI(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 16 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_CORE_STATUS(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 20 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_CORE_RESET(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 24 * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_CORE_CLK_EN(base, max_cores, as_coreid)	\
	(ARCSYNC_V2_CORE_PMODE_BASE(base) + 28 * (max_cores) +	\
	4 * (as_coreid))

#define ARCSYNC_V2_EID_BASE(base)		((base) + 0x4000)
#define ARCSYNC_V2_EID_RAISE_IRQ(base, max_cores, as_coreid, idx)	\
	(ARCSYNC_V2_EID_BASE(base) + (16 + 8 * (idx)) * (max_cores) +	\
	4 * (as_coreid))
#define ARCSYNC_V2_EID_ACK_IRQ(base, max_cores, as_coreid, idx)	\
	(ARCSYNC_V2_EID_BASE(base) + (20 + 8 * (idx)) * (max_cores) +	\
	4 * (as_coreid))

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
				u32 core_id, u32 index)
{
	struct snps_accel_arcsync *arcsync = mgr->priv;
	void __iomem *r_addr, *a_addr;
	u32 target_as_coreid, max_cores;
	unsigned long flags;

	target_as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
						    arcsync->coreid_bw);
	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;

	if (arcsync->version == 1) {
		r_addr = ARCSYNC_V1_EID_RAISE_IRQ(mgr->reg_vbase,
						  max_cores, target_as_coreid,
						  index);
		a_addr = ARCSYNC_V1_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
						target_as_coreid, index);
	} else {
		r_addr = ARCSYNC_V2_EID_RAISE_IRQ(mgr->reg_vbase,
						  max_cores, target_as_coreid,
						  index);
		a_addr = ARCSYNC_V2_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
						target_as_coreid, index);
	}

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (readl(a_addr) == 1U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		return -EBUSY;
	}

	writel(sender_as_coreid, r_addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	return 0;
}

/**
 * snps_accel_arcsync_abort_irq - Abort an interrupt to target core which was not acknowledged.
 * @mgr: Manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @index: Manager interrupt index.
 */
void snps_accel_arcsync_abort_irq(struct snps_accel_mgr *mgr,
				  u32 processor_id, u32 core_id, u32 index)
{
	struct snps_accel_arcsync *arcsync = mgr->priv;
	void __iomem *a_addr;
	u32 target_as_coreid, max_cores;
	unsigned long flags;

	target_as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
						    arcsync->coreid_bw);
	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;

	if (arcsync->version == 1)
		a_addr = ARCSYNC_V1_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
						target_as_coreid, index);
	else
		a_addr = ARCSYNC_V2_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
						target_as_coreid, index);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (readl(a_addr) == 0U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		return;
	}

	writel(target_as_coreid, a_addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);
}

/**
 * snps_accel_arcsync_load_irq - Load value of register EID_RAISE_IRQ.
 * @irq: Manager's IRQ instance.
 *
 * Return: Value of register EID_RAISE_IRQ.
 */
static u32 snps_accel_arcsync_load_irq(struct snps_accel_mgr_irq *irq)
{
	struct snps_accel_mgr *mgr = irq->mgr;
	struct snps_accel_arcsync *arcsync = mgr->priv;
	void __iomem *addr;
	u32 as_coreid, max_cores, result;
	unsigned long flags;

	as_coreid = snps_accel_get_as_coreid(mgr->host_processor_id,
					     mgr->host_core_id,
					     arcsync->coreid_bw);
	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_EID_RAISE_IRQ(mgr->reg_vbase,
						max_cores, as_coreid,
						irq->index);
	else
		addr = ARCSYNC_V2_EID_RAISE_IRQ(mgr->reg_vbase,
						max_cores, as_coreid,
						irq->index);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	result = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	return result;
}

/**
 * snps_accel_arcsync_ack_irq - Acknowledge a manager IRQ.
 * @irq: IRQ instance.
 */
static void snps_accel_arcsync_ack_irq(struct snps_accel_mgr_irq *irq)
{
	struct snps_accel_mgr *mgr = irq->mgr;
	struct snps_accel_arcsync *arcsync = mgr->priv;
	void __iomem *addr;
	u32 as_coreid, max_cores;
	unsigned long flags;

	as_coreid = snps_accel_get_as_coreid(mgr->host_processor_id,
					     mgr->host_core_id,
					     arcsync->coreid_bw);
	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
					      as_coreid, irq->index);
	else
		addr = ARCSYNC_V2_EID_ACK_IRQ(mgr->reg_vbase, max_cores,
					      as_coreid, irq->index);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(as_coreid, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);
}

/**
 * snps_accel_arcsync_irq_base_handler - Base handler of manager IRQ.
 * @irq_id: IRQ ID describing the index in the interrupt vector table.
 * @data: Private data of the IRQ
 *
 * Return: IRQ_HANDLED.
 */
static irqreturn_t snps_accel_arcsync_irq_base_handler(int irq_id, void *data)
{
	struct snps_accel_mgr_irq *irq = data;
	struct snps_accel_mgr_irq_tuple *tuple;
	struct snps_accel_mgr *mgr = irq->mgr;
	struct snps_accel_arcsync *arcsync = mgr->priv;
	u32 sender, processor_id, core_id;
	unsigned long flags;
	int i, count = 0;
	LIST_HEAD(free_tuples);
	struct snps_accel_mgr_irq_tuple *call_tuples[SNPS_ACCEL_MAX_MGR_IRQ_TUPLES];

	sender = snps_accel_arcsync_load_irq(irq);
	snps_accel_arcsync_ack_irq(irq);

	processor_id = snps_accel_as_coreid_to_processor_id(
					sender, arcsync->coreid_bw);
	core_id = snps_accel_as_coreid_to_core_id(sender, arcsync->coreid_bw);

	spin_lock_irqsave(&irq->tuples_lock, flags);
	list_for_each_entry(tuple, &irq->tuples, irq_link) {
		refcount_inc(&tuple->refcount);
		call_tuples[count] = tuple;
		count++;
	}
	spin_unlock_irqrestore(&irq->tuples_lock, flags);

	for (i = 0; i < count; i++) {
		if (call_tuples[i]->handler)
			call_tuples[i]->handler(irq->index, irq_id,
						processor_id, core_id,
						call_tuples[i]->data);

		if (refcount_dec_and_test(&call_tuples[i]->refcount)) {
			spin_lock_irqsave(&irq->tuples_lock, flags);
			list_add_tail(&call_tuples[i]->irq_link,
				      &irq->free_tuples);
			spin_unlock_irqrestore(&irq->tuples_lock, flags);
		}
	}

	schedule_work(&irq->free_work);

	return IRQ_HANDLED;
}

/**
 * snps_accel_arcsync_get_mgr_id - Get ID.
 * @handle: Handle of a manager.
 * @result: Output ID.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_get_mgr_id(void *handle, u32 *result)
{
	struct snps_accel_mgr *mgr;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	*result = mgr->mgr_id;

	dev_dbg(mgr->dev, "Got ID %u.\n", *result);

	return 0;
}

/**
 * snps_accel_arcsync_get_type - Get type.
 *
 * Return: Type.
 */
static enum snps_accel_mgr_type snps_accel_arcsync_get_type(void)
{
	pr_debug("%s: Got type %u.\n", __func__, SNPS_ACCEL_MGR_TYPE_ARCSYNC);
	return SNPS_ACCEL_MGR_TYPE_ARCSYNC;
}

/**
 * snps_accel_arcsync_get_version - Get version.
 * @handle: Handle of a manager.
 * @result: Output version.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_get_version(void *handle, u32 *result)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	*result = arcsync->version;

	dev_dbg(mgr->dev, "Got version %u.\n", *result);

	return 0;
}

/**
 * snps_accel_arcsync_get_num_processors - Get number of processors.
 * @handle: Handle of a manager.
 * @result: Output number of processors.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_get_num_processors(void *handle,
						 u32 *result)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	*result = arcsync->num_processors;

	dev_dbg(mgr->dev, "Got number of processors %u.\n", *result);

	return 0;
}

/**
 * snps_accel_arcsync_get_max_cores_pp - Get maximum number of cores per processor.
 * @handle: Handle of a manager.
 * @result: Output maximum number of cores per processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_max_cores_pp(void *handle, u32 *result)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	*result = arcsync->max_cores_per_processor;

	dev_dbg(mgr->dev, "Got max num of cores per processor %u.\n",
		*result);

	return 0;
}

/**
 * snps_accel_arcsync_get_has_pmu - Get if a manager has PMU.
 * @handle: Handle of a manager.
 * @result: Output %true if a manager has PMU, otherwise %false.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_get_has_pmu(void *handle, bool *result)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	*result = arcsync->has_pmu;

	dev_dbg(mgr->dev, "Got has PMU %s.\n", *result ? "true" : "false");

	return 0;
}

/**
 * snps_accel_arcsync_get_num_cores_of_processor - Get the number of cores of a processor.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @result: Output number of cores of a processor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_arcsync_get_num_cores_of_processor(void *handle,
						  u32 processor_id,
						  u32 *result)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!result)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors)) {
		dev_err(mgr->dev, "Invalid processor %u.\n",
			processor_id);
		return -EINVAL;
	}

	if (processor_id < 4)
		addr = ARCSYNC_NUM_CORE_0_3(mgr->reg_vbase);
	else if (processor_id < 8)
		addr = ARCSYNC_NUM_CORE_4_7(mgr->reg_vbase);
	else if (processor_id < 12) {
		if (arcsync->version == 1) {
			dev_err(mgr->dev, "Unsupported processor %u.\n",
				processor_id);
			return -ENOTSUPP;
		}
		addr = ARCSYNC_NUM_CORE_8_11(mgr->reg_vbase);
	} else if (processor_id < 16) {
		if (arcsync->version == 1) {
			dev_err(mgr->dev, "Unsupported processor %u.\n",
				processor_id);
			return -ENOTSUPP;
		}
		addr = ARCSYNC_NUM_CORE_12_15(mgr->reg_vbase);
	} else {
		dev_err(mgr->dev, "Unsupported processor %u.\n",
			processor_id);
		return -ENOTSUPP;
	}

	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	*result = ((value >> ((processor_id & 3U) << 3U)) & 0xffU);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_boot_addr - Set boot address.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @boot_addr_64bit: A 64bit boot address or not.
 * @boot_ivb: Boot address.
 */
static int
snps_accel_arcsync_set_core_boot_addr(void *handle, u32 processor_id,
				      u32 core_id, bool boot_addr_64bit,
				      u64 boot_addr)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 as_coreid, max_cores, lo_value, hi_value, align_bw;
	void __iomem *lo_addr, *hi_addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);
	max_cores = arcsync->num_processors * arcsync->max_cores_per_processor;
	lo_addr = (arcsync->version == 1) ?
		ARCSYNC_V1_BOOT_IVB_LO(mgr->reg_vbase, max_cores, as_coreid) :
		ARCSYNC_V2_BOOT_IVB_LO(mgr->reg_vbase, max_cores, as_coreid);
	hi_addr = (arcsync->version == 1) ?
		ARCSYNC_V1_BOOT_IVB_HI(mgr->reg_vbase, max_cores, as_coreid) :
		ARCSYNC_V2_BOOT_IVB_HI(mgr->reg_vbase, max_cores, as_coreid);
	lo_value = (u32)(boot_addr & 0xffffffff);
	hi_value = (u32)((boot_addr >> 32) & 0xffffffff);
	align_bw = boot_addr_64bit ? 11 : 10;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (boot_addr_64bit) {
		writel(lo_value >> align_bw, lo_addr);
		writel(hi_value, hi_addr);
	} else {
		writel(lo_value >> align_bw, lo_addr);
		writel(0U, hi_addr);
	}
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Set %s boot address %#llx of core %u_%u.\n",
		boot_addr_64bit ? "64-bit" : "32-bit", boot_addr,
		processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_boot_addr - Get boot address.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @boot_addr_64bit: A 64bit boot address or not.
 * @boot_addr: Output boot address.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_core_boot_addr(void *handle, u32 processor_id,
				      u32 core_id, bool boot_addr_64bit,
				      u64 *boot_addr)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 as_coreid, max_cores, align_bw;
	u64 lo_value, hi_value;
	void __iomem *lo_addr, *hi_addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!boot_addr)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);
	max_cores = arcsync->num_processors * arcsync->max_cores_per_processor;
	lo_addr = (arcsync->version == 1) ?
		ARCSYNC_V1_BOOT_IVB_LO(mgr->reg_vbase, max_cores, as_coreid) :
		ARCSYNC_V2_BOOT_IVB_LO(mgr->reg_vbase, max_cores, as_coreid);
	hi_addr = (arcsync->version == 1) ?
		ARCSYNC_V1_BOOT_IVB_HI(mgr->reg_vbase, max_cores, as_coreid) :
		ARCSYNC_V2_BOOT_IVB_HI(mgr->reg_vbase, max_cores, as_coreid);
	align_bw = boot_addr_64bit ? 11 : 10;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (boot_addr_64bit) {
		lo_value = readl(lo_addr);
		hi_value = readl(hi_addr);
		*boot_addr = (hi_value << 32) | (lo_value << align_bw);
	} else {
		*boot_addr = readl(lo_addr) << align_bw;
	}
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Got %s boot address %#llx of core %u_%u.\n",
		boot_addr_64bit ? "64-bit" : "32-bit", *boot_addr,
		processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_clock - Set core clock.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @enable: Enable core clock or not.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_set_core_clock(void *handle, u32 processor_id,
					     u32 core_id, bool enable)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, max_cores, as_coreid;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return  -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	if (arcsync->version == 1) {
		addr = ARCSYNC_V1_CGATE(mgr->reg_vbase,
			arcsync->num_processors, processor_id);
		spin_lock_irqsave(&mgr->reg_lock, flags);
		value = readl(addr);
		if (enable) {
			if ((value & (1U << core_id)) == 0)
				writel(value | (1U << core_id), addr);
		} else {
			if ((value & (1U << core_id)) != 0)
				writel(value & (~(1U << core_id)), addr);
		}
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
	} else {
		max_cores = arcsync->num_processors *
			    arcsync->max_cores_per_processor;
		as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
						     arcsync->coreid_bw);
		addr = ARCSYNC_V2_CORE_CLK_EN(mgr->reg_vbase,
					      max_cores, as_coreid);
		spin_lock_irqsave(&mgr->reg_lock, flags);
		writel(enable ? 1U : 0U, addr);
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
	}

	dev_dbg(mgr->dev, "%s clock of core %u_%u.\n",
		enable ? "Enabled" : "Disabled", processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_clock - Get core clock
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @enabled: Output enabled or not
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_core_clock(void *handle, u32 processor_id,
				  u32 core_id, bool *enabled)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, max_cores, as_coreid;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!enabled)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	if (arcsync->version == 1) {
		addr = ARCSYNC_V1_CGATE(mgr->reg_vbase,
					arcsync->num_processors,
					processor_id);
		spin_lock_irqsave(&mgr->reg_lock, flags);
		value = readl(addr);
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		*enabled = ((value & (1U << core_id)) == 1) ? true : false;
	} else {
		max_cores = arcsync->num_processors *
			    arcsync->max_cores_per_processor;
		as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
						     arcsync->coreid_bw);
		addr = ARCSYNC_V2_CORE_CLK_EN(mgr->reg_vbase, max_cores,
					      as_coreid);
		spin_lock_irqsave(&mgr->reg_lock, flags);
		value = readl(addr);
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		*enabled = ((value & 1) == 1) ? true : false;
	}

	dev_dbg(mgr->dev, "Got clock status '%s' of core %u_%u.\n",
		*enabled ? "enabled" : "disabled", processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_group_clock - Set clock of a core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @group_id: Target core group ID.
 * @enable: Whether to enable clock of a core group or not.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_core_group_clock(void *handle, u32 processor_id,
					u32 group_id, bool enable)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, password;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     group_id >= SNPS_ACCEL_NPX_MAX_GROUPS)) {
		dev_err(mgr->dev, "Invalid processor or core group ID %u_%u.\n",
			processor_id, group_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_CLK_EN(mgr->reg_vbase,
					arcsync->num_processors, processor_id);
	password = enable ? 0xa5a50000 : 0x5a5a0000;
	value = password | (group_id + 1U) << (3 * (group_id + 1));

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev,
		"%s clock of core group %u_%u.\n",
		enable ? "Enabled" : "Disabled", processor_id, group_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_group_clock - Get clock status of a core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @group_id: Target core group ID.
 * @enabled: Output enabled or not
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_core_group_clock(void *handle, u32 processor_id,
					u32 group_id, bool *enabled)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!enabled)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     group_id >= SNPS_ACCEL_NPX_MAX_GROUPS)) {
		dev_err(mgr->dev, "Invalid processor or core group ID %u_%u.\n",
			processor_id, group_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_CLK_EN(mgr->reg_vbase, arcsync->num_processors,
					processor_id);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	value = (value >> (3 * (group_id + 1))) & 7;
	*enabled = (value == 1) ? true : false;

	dev_dbg(mgr->dev, "Got clock status '%s' of core group %u_%u.\n",
		*enabled ? "enabled" : "disabled", processor_id, group_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_vpx_block_clock - Set clock of a VPX block.
 * @handle: Handle of a manager.
 * @vpx_block_id: Target VPX block ID.
 * @enable: Whether to enable clock of a VPX block or not.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_vpx_block_clock(void *handle, u32 vpx_block_id,
				       bool enable)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(vpx_block_id >= arcsync->num_vpx_blocks)) {
		dev_err(mgr->dev, "Invalid VPX block ID %u.\n", vpx_block_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_VPX_BLOCK_CLK_EN(mgr->reg_vbase,
					   arcsync->num_vpx_blocks,
					   vpx_block_id);
	value = enable ? 1 : 0;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev,
		"%s clock of VPX block %u.\n",
		enable ? "Enabled" : "Disabled", vpx_block_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_vpx_block_clock - Get clock status of a VPX block.
 * @handle: Handle of a manager.
 * @vpx_block_id: Target VPX block ID.
 * @enabled: Output enabled or not
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_vpx_block_clock(void *handle, u32 vpx_block_id,
				       bool *enabled)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!enabled)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(vpx_block_id >= arcsync->num_vpx_blocks)) {
		dev_err(mgr->dev, "Invalid VPX block ID %u.\n", vpx_block_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_VPX_BLOCK_CLK_EN(mgr->reg_vbase,
					   arcsync->num_vpx_blocks,
					   vpx_block_id);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	*enabled = ((value & 1) == 1) ? true : false;

	dev_dbg(mgr->dev, "Got clock status '%s' of VPX block %u.\n",
		*enabled ? "enabled" : "disabled", vpx_block_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_reset - Set a core-reset.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @assert: Assert or deassert.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_set_core_reset(void *handle, u32 processor_id,
					     u32 core_id, bool assert)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, max_cores, as_coreid, password;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_warn(mgr->dev, "%s is not supported for the manager.\n",
			 __func__);
		return -ENOTSUPP;
	}

	max_cores = arcsync->num_processors *
			arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);
	addr = ARCSYNC_V2_CORE_RESET(mgr->reg_vbase, max_cores, as_coreid);
	password = assert ? 0x5a5a0000 : 0xa5a50000;
	value = password | (as_coreid & 0xffff);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "%s reset of core %u_%u.\n",
		assert ? "Asserted" : "Deasserted", processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_reset_core - Reset a core
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_reset_core(void *handle, u32 processor_id,
					 u32 core_id)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 assert, deassert, max_cores, as_coreid;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1) {
		addr = ARCSYNC_V1_CORE_RESET(mgr->reg_vbase, max_cores,
					     as_coreid);
		assert = 0x5a5a0000 | (as_coreid & 0xffff);

		spin_lock_irqsave(&mgr->reg_lock, flags);
		writel(assert, addr);
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
	} else {
		addr = ARCSYNC_V2_CORE_RESET(mgr->reg_vbase, max_cores,
					     as_coreid);
		assert = 0x5a5a0000 | (as_coreid & 0xffff);
		deassert = 0xa5a50000 | (as_coreid & 0xffff);
		spin_lock_irqsave(&mgr->reg_lock, flags);
		writel(assert, addr);
		udelay(10);
		writel(deassert, addr);
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
	}

	dev_dbg(mgr->dev, "Reset core %u_%u.\n", processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_reset_status - Get status of a core-reset.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @status: Version 1: Output 1 if the core is currently under reset and not ready
 *                     output 0 if the core is not under reset.
 *          Version 2: Output 1 if the reset pin is currently asserted, output 0 otherwise.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_core_reset_status(void *handle, u32 processor_id,
					 u32 core_id, u32 *status)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 max_cores, as_coreid;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!status)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_CORE_RESET(mgr->reg_vbase, max_cores,
					     as_coreid);
	else
		addr = ARCSYNC_V2_CORE_RESET(mgr->reg_vbase, max_cores,
					     as_coreid);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	*status = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Got reset status '%u' of core %u_%u.\n",
		*status, processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_group_reset - Set reset of a core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @group_id: Target core group ID.
 * @assert: Assert or deassert.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_core_group_reset(void *handle, u32 processor_id,
					u32 group_id, bool assert)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, password;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     group_id >= SNPS_ACCEL_NPX_MAX_GROUPS)) {
		dev_err(mgr->dev, "Invalid processor or core group ID %u_%u.\n",
			processor_id, group_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_RESET(mgr->reg_vbase, arcsync->num_processors,
				       processor_id);
	password = assert ? 0x5a5a0000 : 0xa5a50000;
	value = password | ((group_id + 1) << (3 * (group_id + 1)));

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev,
		"%s reset of core group %u_%u.\n",
		assert ? "Asserted" : "Deasserted", processor_id, group_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_group_reset_status - Get reset status of a core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @group_id: Target core group ID.
 * @status: Output 1 if the reset pin is currently asserted, output 0 otherwise.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_core_group_reset_status(void *handle, u32 processor_id,
					       u32 group_id, u32 *status)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!status)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     group_id >= SNPS_ACCEL_NPX_MAX_GROUPS)) {
		dev_err(mgr->dev, "Invalid processor or core group ID %u_%u.\n",
			processor_id, group_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_RESET(mgr->reg_vbase, arcsync->num_processors,
				       processor_id);
	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	*status = (value >> (3 * (group_id + 1))) & 7;

	dev_dbg(mgr->dev, "Got reset status '%s' of core group %u_%u.\n",
		*status == 1 ? "asserted" : "deasserted", processor_id,
		group_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_l2_core_group_reset - Set reset of L2 core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @assert: Assert or deassert.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_l2_core_group_reset(void *handle, u32 processor_id,
					   bool assert)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, password;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors)) {
		dev_err(mgr->dev, "Invalid processor ID %u.\n", processor_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_RESET(mgr->reg_vbase,
					arcsync->num_processors,
					processor_id);
	password = assert ? 0x5a5a0000 : 0xa5a50000;
	value = password | 5U;
	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev,
		"%s reset of processor %u L2 core group.\n",
		assert ? "Asserted" : "Deasserted", processor_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_l2_core_group_reset_status - Get reset status of L2 core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @status: Output 1 if the reset pin is currently asserted, output 0 otherwise.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_l2_core_group_reset_status(void *handle, u32 processor_id,
						  u32 *status)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!status)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors)) {
		dev_err(mgr->dev, "Invalid processor ID %u.\n", processor_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_RESET(mgr->reg_vbase, arcsync->num_processors,
				       processor_id);
	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	*status = value & 7U;

	dev_dbg(mgr->dev,
		"Got reset status '%s' of processor %u L2 core group.\n",
		*status == 1 ? "asserted" : "deasserted", processor_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_vpx_block_reset - Set reset of a VPX block.
 * @handle: Handle of a manager.
 * @vpx_block_id: Target VPX block ID.
 * @assert: Assert or deassert.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_vpx_block_reset(void *handle, u32 vpx_block_id,
				       bool assert)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, password;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(vpx_block_id >= arcsync->num_vpx_blocks)) {
		dev_err(mgr->dev, "Invalid VPX block ID %u.\n", vpx_block_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_VPX_BLOCK_RESET(mgr->reg_vbase,
					  arcsync->num_vpx_blocks,
					  vpx_block_id);
	password = assert ? 0x5a5a0000 : 0xa5a50000;
	value = password | vpx_block_id;
	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(value, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev,
		"%s reset of VPX block %u.\n",
		assert ? "Asserted" : "Deasserted", vpx_block_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_vpx_block_reset_status - Get reset status of a VPX block.
 * @handle: Handle of a manager.
 * @vpx_block_id: Target VPX block ID.
 * @status: Output 1 if the reset pin is currently asserted, output 0 otherwise.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_vpx_block_reset_status(void *handle, u32 vpx_block_id,
					      u32 *status)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!status)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(vpx_block_id >= arcsync->num_vpx_blocks)) {
		dev_err(mgr->dev, "Invalid VPX block ID %u.\n", vpx_block_id);
		return -EINVAL;
	}

	if (unlikely(arcsync->version == 1)) {
		dev_err(mgr->dev, "%s is not supported for the manager.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_VPX_BLOCK_RESET(mgr->reg_vbase,
					  arcsync->num_vpx_blocks,
					  vpx_block_id);
	spin_lock_irqsave(&mgr->reg_lock, flags);
	value = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	*status = value & 0xffffU;

	dev_dbg(mgr->dev,
		"Got reset status '%s' of VPX block %u.\n",
		*status == 1 ? "asserted" : "deasserted", vpx_block_id);

	return 0;
}

/**
 * snps_accel_arcsync_halt_core - Halt a core.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @check_finish: Whether to check finish.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_halt_core(void *handle, u32 processor_id, u32 core_id,
			     bool check_finish)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 max_cores, as_coreid, count = 10;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_CORE_HALT(mgr->reg_vbase, max_cores,
					    as_coreid);
	else
		addr = ARCSYNC_V2_CORE_HALT(mgr->reg_vbase, max_cores,
					    as_coreid);

	spin_lock_irqsave(&mgr->reg_lock, flags);

	/*
	 * Before sending a new halt command, software must ensure there is
	 * not a halt handshake already ongoing with that core.
	 */
	if (readl(addr) & 1U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		dev_err(mgr->dev, "Core %u_%u halt-handshake is ongoing\n",
			processor_id, core_id);
		return -EBUSY;
	}

	writel(1U, addr);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	while (readl(addr) & 1U)
		udelay(1);
#else
	if (check_finish)
		while ((readl(addr) & 1U) && --count)
			udelay(1);
#endif
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	if (!count)
		dev_warn(mgr->dev, "Still halting core %u_%u.\n",
			 processor_id, core_id);
	else
		dev_dbg(mgr->dev, "Halt core %u_%u.\n", processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_run_core - Run a core.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @check_finish: Whether to check finish.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_run_core(void *handle, u32 processor_id, u32 core_id,
			    bool check_finish)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 max_cores, as_coreid, count = 10;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_CORE_RUN(mgr->reg_vbase, as_coreid);
	else
		addr = ARCSYNC_V2_CORE_RUN(mgr->reg_vbase, max_cores,
					   as_coreid);

	spin_lock_irqsave(&mgr->reg_lock, flags);

	/*
	 * Before sending a new run command, software must ensure there is
	 * no ongoing run handshake already ongoing with the core.
	 */
	if (readl(addr) & 1U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		dev_err(mgr->dev, "Core %u_%u run-handshake is ongoing\n",
			processor_id, core_id);
		return -EBUSY;
	}

	writel(1U, addr);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	while (readl(addr) & 1U)
		udelay(1);
#else
	if (check_finish)
		while ((readl(addr) & 1U) && --count)
			udelay(1);
#endif
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	if (unlikely(!count))
		dev_warn(mgr->dev, "Still running core %u_%u.\n",
			 processor_id, core_id);
	else
		dev_dbg(mgr->dev, "Run core %u_%u.\n", processor_id,
			core_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_core_status - Get status of a core.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @status: Status of a core.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_arcsync_get_core_status(void *handle, u32 processor_id,
					      u32 core_id, u32 *status)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 max_cores, as_coreid;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!status)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1)
		addr = ARCSYNC_V1_CORE_STATUS(mgr->reg_vbase,
					      max_cores, as_coreid);
	else
		addr = ARCSYNC_V2_CORE_STATUS(mgr->reg_vbase,
					      max_cores, as_coreid);

	spin_lock_irqsave(&mgr->reg_lock, flags);
	*status = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Got status %u of core %u_%u.\n", *status,
		processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_power_mode - Get power mode of a core.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @core_id: Target core ID.
 * @pmode: Power mode.
 * @check_finish: Whether to check finish.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_core_power_mode(void *handle, u32 processor_id,
				       u32 core_id,
				       enum snps_accel_pmode pmode,
				       bool check_finish)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	u32 value, max_cores, as_coreid, count = 20;
	void __iomem *addr;
	unsigned long flags;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     core_id >= arcsync->max_cores_per_processor)) {
		dev_err(mgr->dev, "Invalid processor or core ID %u_%u.\n",
			processor_id, core_id);
		return -EINVAL;
	}

	if (unlikely(!arcsync->has_pmu)) {
		dev_err(mgr->dev,
			"%s is not supported due to without PMU.\n",
			__func__);
		return -ENOTSUPP;
	}

	max_cores = arcsync->num_processors *
		    arcsync->max_cores_per_processor;
	as_coreid = snps_accel_get_as_coreid(processor_id, core_id,
					     arcsync->coreid_bw);

	if (arcsync->version == 1) {
		addr = ARCSYNC_V1_CORE_PMODE(mgr->reg_vbase, max_cores,
					     as_coreid);
		value = (pmode == SNPS_ACCEL_PMODE_UP) ? 1 : 2;
	} else {
		addr = ARCSYNC_V2_CORE_PMODE(mgr->reg_vbase, as_coreid);
		value = (pmode == SNPS_ACCEL_PMODE_UP) ? 0 : 1;
	}

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if ((arcsync->version > 1) && (readl(addr) & 1U)) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		dev_err(mgr->dev,
			"Cannot set power mode %u of busy core %u_%u.\n",
			pmode, processor_id, core_id);
		return -EBUSY;
	}

	writel(value, addr);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	if (arcsync->version > 1)
		while (readl(addr) & 1U)
			udelay(1);
#else
	if (check_finish)
		while ((readl(addr) & 1U) && --count)
			udelay(1);
#endif
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	if (unlikely(!count))
		dev_warn(mgr->dev,
			 "Still setting power mode %u of core %u_%u.\n",
			 pmode, processor_id, core_id);
	else
		dev_dbg(mgr->dev, "Set power mode %u of core %u_%u.\n",
			pmode, processor_id, core_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_core_group_power_mode - Set power mode of a core group.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @group_id: Target core group ID.
 * @pmode: Power mode.
 * @check_finish: Whether to check finish.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_core_group_power_mode(void *handle, u32 processor_id,
					     u32 group_id,
					     enum snps_accel_pmode pmode,
					     bool check_finish)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value, count = 20;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors ||
		     group_id >= SNPS_ACCEL_NPX_MAX_GROUPS)) {
		dev_err(mgr->dev, "Invalid processor or core group ID %u_%u.\n",
			processor_id, group_id);
		return -EINVAL;
	}

	if (unlikely(!arcsync->has_pmu || arcsync->version == 1)) {
		dev_err(mgr->dev,
			"%s is not supported due to without PMU.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_CL_GRP_PMODE(mgr->reg_vbase,
					arcsync->num_processors,
					processor_id, group_id);
	value = (pmode == SNPS_ACCEL_PMODE_UP) ? 0 : 1;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (readl(addr) & 1U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		dev_err(mgr->dev,
			"Cannot set power mode %u of busy core group %u_%u.\n",
			pmode, processor_id, group_id);
		return -EBUSY;
	}

	writel(value, addr);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	while (readl(addr) & 1U)
		udelay(1);

	if (((readl(addr) >> 1) & 1U) != value)
		BUG_ON(1);
#else
	if (check_finish)
		while ((readl(addr) & 1U) && --count)
			udelay(1);
#endif
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	if (unlikely(!count))
		dev_warn(mgr->dev,
			 "Still setting power mode %u of core group %u_%u.\n",
			 pmode, processor_id, group_id);
	else
		dev_dbg(mgr->dev, "Set power mode %u of core group %u_%u.\n",
			pmode, processor_id, group_id);

	return 0;
}

/**
 * snps_accel_arcsync_set_vpx_block_power_mode - Set power mode of a VPX block.
 * @handle: Handle of a manager.
 * @vpx_block_id: Target VPX block ID.
 * @pmode: Power mode.
 * @check_finish: Whether to check finish.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_vpx_block_power_mode(void *handle, u32 vpx_block_id,
					    enum snps_accel_pmode pmode,
					    bool check_finish)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;
	u32 value, count = 20;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(vpx_block_id >= arcsync->num_vpx_blocks)) {
		dev_err(mgr->dev, "Invalid VPX block ID %u.\n", vpx_block_id);
		return -EINVAL;
	}

	if (unlikely(!arcsync->has_pmu || arcsync->version == 1)) {
		dev_err(mgr->dev,
			"%s is not supported due to without PMU.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = ARCSYNC_V2_PMU_VPX_BLOCK_PMODE(mgr->reg_vbase, vpx_block_id);
	value = (pmode == SNPS_ACCEL_PMODE_UP) ? 0 : 1;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	if (readl(addr) & 1U) {
		spin_unlock_irqrestore(&mgr->reg_lock, flags);
		dev_err(mgr->dev,
			"Cannot set power mode %u of busy VPX block %u.\n",
			pmode, vpx_block_id);
		return -EBUSY;
	}

	writel(value, addr);

#ifdef CONFIG_DRM_ACCEL_SNPS_DEBUG
	while (readl(addr) & 1U)
		udelay(1);
#else
	if (check_finish)
		while ((readl(addr) & 1U) && --count)
			udelay(1);
#endif
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	if (unlikely(!count))
		dev_warn(mgr->dev,
			 "Still setting power mode %u of VPX block %u.\n",
			 pmode, vpx_block_id);
	else
		dev_dbg(mgr->dev, "Set power mode %u of VPX block %u.\n",
			pmode, vpx_block_id);

	return 0;
}

/**
 * snps_accel_arcsync_pmu_count_addr - Get address of PMU_SET_* registers.
 * @arcsync: Handle of a manager.
 * @processor_id: Target processor ID.
 * @type: Type of PMU count.
 */
static void __iomem *
snps_accel_arcsync_pmu_count_addr(struct snps_accel_arcsync *arcsync,
				  u32 processor_id,
				  enum snps_accel_mgr_pmu_cnt_type type)
{
	struct snps_accel_mgr *mgr = arcsync->mgr;
	void __iomem *addr;
	u32 max_cores;

	if (arcsync->version == 1) {
		switch (type) {
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_RST:
			addr = ARCSYNC_V1_PMU_SET_RSTCNT(mgr->reg_vbase);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_PU:
			addr = ARCSYNC_V1_PMU_SET_PUCNT(mgr->reg_vbase);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_PD:
			addr = ARCSYNC_V1_PMU_SET_PDCNT(mgr->reg_vbase);
			break;
		default:
			return NULL;
		}
	} else {
		max_cores = arcsync->num_processors *
			    arcsync->max_cores_per_processor;

		switch (type) {
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_RST:
			addr = ARCSYNC_V2_PMU_SET_RSTCNT(mgr->reg_vbase,
							 max_cores,
							 processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_PU:
			addr = ARCSYNC_V2_PMU_SET_PUCNT(mgr->reg_vbase,
							max_cores,
							processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_PD:
			addr = ARCSYNC_V2_PMU_SET_PDCNT(mgr->reg_vbase,
							max_cores,
							processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG1:
			addr = ARCSYNC_V2_PMU_CORE_LOGIC1(mgr->reg_vbase,
							  max_cores,
							  processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_CORE_LG2:
			addr = ARCSYNC_V2_PMU_CORE_LOGIC2(mgr->reg_vbase,
							  max_cores,
							  processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG1:
			addr = ARCSYNC_V2_PMU_GRP_LOGIC1(mgr->reg_vbase,
							 max_cores,
							 processor_id);
			break;
		case SNPS_ACCEL_MGR_PMU_CNT_TYPE_GRP_LG2:
			addr = ARCSYNC_V2_PMU_GRP_LOGIC2(mgr->reg_vbase,
							 max_cores,
							 processor_id);
			break;
		default:
			return NULL;
		}
	}

	return addr;
}

/**
 * snps_accel_arcsync_set_pmu_count - Set PMU count.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @type: Type of PMU count.
 * @count: PMU count.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_set_pmu_count(void *handle, u32 processor_id,
				 enum snps_accel_mgr_pmu_cnt_type type,
				 u32 count)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors)) {
		dev_err(mgr->dev, "Invalid processor ID %u.\n", processor_id);
		return -EINVAL;
	}

	if (unlikely(!arcsync->has_pmu)) {
		dev_err(mgr->dev,
			"%s is not supported due to without PMU.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = snps_accel_arcsync_pmu_count_addr(arcsync, processor_id, type);
	if (unlikely(!addr))
		return -ENOTSUPP;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	writel(count, addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Set PMU %u count %u of processor %u.\n",
		type, count, processor_id);

	return 0;
}

/**
 * snps_accel_arcsync_get_pmu_count - Get PMU count.
 * @handle: Handle of a manager.
 * @processor_id: Target processor ID.
 * @type: Type of PMU count.
 * @count: Output PMU count.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_get_pmu_count(void *handle, u32 processor_id,
				 enum snps_accel_mgr_pmu_cnt_type type,
				 u32 *count)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	unsigned long flags;
	void __iomem *addr;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	if (unlikely(!count)) {
		pr_err("%s: Invalid output.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	arcsync = mgr->priv;
	if (unlikely(processor_id >= arcsync->num_processors)) {
		dev_err(mgr->dev, "Invalid processor ID %u.\n", processor_id);
		return -EINVAL;
	}

	if (unlikely(!arcsync->has_pmu)) {
		dev_err(mgr->dev,
			"%s is not supported due to without PMU.\n",
			__func__);
		return -ENOTSUPP;
	}

	addr = snps_accel_arcsync_pmu_count_addr(arcsync, processor_id, type);
	if (unlikely(!addr))
		return -ENOTSUPP;

	spin_lock_irqsave(&mgr->reg_lock, flags);
	*count = readl(addr);
	spin_unlock_irqrestore(&mgr->reg_lock, flags);

	dev_dbg(mgr->dev, "Got PMU %u count %u of processor %u .\n",
		type, *count, processor_id);

	return 0;
}

/**
 * snps_accel_arcsync_register_irq_handler - Register a IRQ handler.
 * @handle: Handle of a manager.
 * @handler: IRQ handler.
 * @data: Private data as an argument of the IRQ handler.
 * @index: The index of manager's IRQ(s) ([0, 3] is a valid value range).
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_register_irq_handler(void *handle,
					snps_accel_mgr_irq_handler handler,
					void *data, u32 index)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_mgr_irq *irq = NULL;
	struct snps_accel_mgr_irq_tuple *tuple;
	unsigned long flags;
	int i;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < mgr->num_irqs; i++) {
		if (mgr->irqs[i].index == index) {
			irq = &mgr->irqs[i];
			break;
		}
	}

	if (unlikely(!irq)) {
		dev_err(mgr->dev, "Unknown IRQ #%u handler (%pS %pS).\n",
			index, handler, data);
		return -ENODEV;
	}

	tuple = kzalloc(sizeof(*tuple), GFP_KERNEL);
	if (!tuple)
		return -ENOMEM;

	INIT_LIST_HEAD(&tuple->irq_link);
	refcount_set(&tuple->refcount, 1);
	tuple->handler = handler;
	tuple->data = data;

	spin_lock_irqsave(&irq->tuples_lock, flags);
	if (irq->num_tuples >= SNPS_ACCEL_MAX_MGR_IRQ_TUPLES) {
		spin_unlock_irqrestore(&irq->tuples_lock, flags);
		kfree(tuple);
		dev_err(mgr->dev,
			"No space to register IRQ #%u handler (%pS %pS).\n",
			index, handler, data);
		return -ENOSPC;
	}

	list_add_tail(&tuple->irq_link, &irq->tuples);
	irq->num_tuples++;
	spin_unlock_irqrestore(&irq->tuples_lock, flags);

	dev_dbg(mgr->dev, "Registered IRQ #%u handler (%pS %pS).\n",
		index, handler, data);

	return 0;
}

/**
 * snps_accel_arcsync_deregister_irq_handler - Deregister a IRQ handler.
 * @handle: Handle of a manager.
 * @handler: IRQ handler.
 * @index: The index of manager's IRQ(s) ([0, 3] is a valid value range).
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_arcsync_deregister_irq_handler(void *handle,
					  snps_accel_mgr_irq_handler handler,
					  u32 index)
{
	struct snps_accel_mgr *mgr;
	struct snps_accel_mgr_irq *irq = NULL;
	struct snps_accel_mgr_irq_tuple *tuple, *next;
	LIST_HEAD(free_tuples);
	unsigned long flags;
	int i;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < mgr->num_irqs; i++) {
		if (mgr->irqs[i].index == index) {
			irq = &mgr->irqs[i];
			break;
		}
	}

	if (!irq) {
		dev_err(mgr->dev, "Unknown IRQ #%u handler (%pS).\n",
			index, handler);
		return -ENODEV;
	}

	spin_lock_irqsave(&irq->tuples_lock, flags);
	list_for_each_entry_safe(tuple, next, &irq->tuples, irq_link) {
		if (tuple->handler == handler) {
			list_del_init(&tuple->irq_link);
			irq->num_tuples--;
			if (refcount_dec_and_test(&tuple->refcount))
				list_add_tail(&tuple->irq_link, &free_tuples);
			break;
		}
	}
	spin_unlock_irqrestore(&irq->tuples_lock, flags);

	list_for_each_entry_safe(tuple, next, &free_tuples, irq_link) {
		list_del_init(&tuple->irq_link);
		kfree(tuple);
	}

	dev_dbg(mgr->dev, "Deregistered IRQ #%u handler (%pS).\n",
		index, handler);

	return 0;
}

static const struct snps_accel_mgr_ops snps_accel_arcsync_ops = {
	.get_mgr_id = snps_accel_arcsync_get_mgr_id,
	.get_type = snps_accel_arcsync_get_type,
	.get_version = snps_accel_arcsync_get_version,
	.get_num_processors = snps_accel_arcsync_get_num_processors,
	.get_max_cores_per_processor = snps_accel_arcsync_get_max_cores_pp,
	.get_has_pmu = snps_accel_arcsync_get_has_pmu,
	.get_num_cores_of_processor =
			snps_accel_arcsync_get_num_cores_of_processor,
	.set_core_boot_addr = snps_accel_arcsync_set_core_boot_addr,
	.get_core_boot_addr = snps_accel_arcsync_get_core_boot_addr,
	.set_core_clock = snps_accel_arcsync_set_core_clock,
	.get_core_clock = snps_accel_arcsync_get_core_clock,
	.set_core_group_clock = snps_accel_arcsync_set_core_group_clock,
	.get_core_group_clock = snps_accel_arcsync_get_core_group_clock,
	.set_vpx_block_clock = snps_accel_arcsync_set_vpx_block_clock,
	.get_vpx_block_clock = snps_accel_arcsync_get_vpx_block_clock,
	.set_core_reset = snps_accel_arcsync_set_core_reset,
	.reset_core = snps_accel_arcsync_reset_core,
	.get_core_reset_status = snps_accel_arcsync_get_core_reset_status,
	.set_core_group_reset = snps_accel_arcsync_set_core_group_reset,
	.get_core_group_reset_status =
			snps_accel_arcsync_get_core_group_reset_status,
	.set_l2_core_group_reset = snps_accel_arcsync_set_l2_core_group_reset,
	.get_l2_core_group_reset_status =
			snps_accel_arcsync_get_l2_core_group_reset_status,
	.set_vpx_block_reset = snps_accel_arcsync_set_vpx_block_reset,
	.get_vpx_block_reset_status =
				snps_accel_arcsync_get_vpx_block_reset_status,
	.halt_core = snps_accel_arcsync_halt_core,
	.run_core = snps_accel_arcsync_run_core,
	.get_core_status = snps_accel_arcsync_get_core_status,
	.set_core_power_mode = snps_accel_arcsync_set_core_power_mode,
	.set_core_group_power_mode =
				snps_accel_arcsync_set_core_group_power_mode,
	.set_vpx_block_power_mode =
				snps_accel_arcsync_set_vpx_block_power_mode,
	.set_pmu_count = snps_accel_arcsync_set_pmu_count,
	.get_pmu_count = snps_accel_arcsync_get_pmu_count,
	.register_irq_handler = snps_accel_arcsync_register_irq_handler,
	.deregister_irq_handler = snps_accel_arcsync_deregister_irq_handler,
};

/**
 * snps_accel_mgr_custom_ops_register - Register custom operations to a manager.
 * @handle: Handle of a manager.
 * @custom_ops: Custom operations.
 * @custom_ops_data: Private data as an argument of custom operations.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_mgr_custom_ops_register(void *handle,
				       struct snps_accel_custom_ops *custom_ops,
				       void *custom_ops_data)
{
	struct snps_accel_mgr *mgr;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return -EINVAL;
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return -ENODEV;
	}

	mgr->custom_ops = custom_ops;
	mgr->custom_ops_data = custom_ops_data;

	dev_info(mgr->dev, "Registered custom operations (%p %p).\n",
		 custom_ops, custom_ops_data);

	return 0;
}
EXPORT_SYMBOL(snps_accel_mgr_custom_ops_register);

/**
 * snps_accel_get_mgr_handle - Get handle of a manager.
 * @compatible: Compatible string defines a manager in DTS (e.g., "snps,accel-arcsync").
 * @mgr_id: Manager ID.
 *
 * Users must put the got handle after the use.
 *
 * Return: Handle of a manager on success, otherwise an appropriate failure.
 */
void *snps_accel_get_mgr_handle(const char *compatible, u32 mgr_id)
{
	void *result;

	if (unlikely(!compatible)) {
		pr_err("%s: Invalid compatible.\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	if (!try_module_get(THIS_MODULE)) {
		pr_err("%s: Failed to get module.\n", __func__);
		return ERR_PTR(-EBUSY);
	}

	result = snps_accel_get_device(compatible, "snps,accel-mgr-id",
				       mgr_id);
	if (IS_ERR(result))
		pr_err("Failed to get handle of manager %u. %ld\n",
		       mgr_id, PTR_ERR(result));
	else
		pr_info("Got handle of manager %u.\n", mgr_id);

	return result;
}
EXPORT_SYMBOL(snps_accel_get_mgr_handle);

/**
 * snps_accel_put_mgr_handle - Put handle of a manager.
 * @handle: Handle of a manager.
 */
void snps_accel_put_mgr_handle(void *handle)
{
	if (handle) {
		snps_accel_put_device(handle);
		module_put(THIS_MODULE);
	}

	pr_info("Put handle %p.\n", handle);
}
EXPORT_SYMBOL(snps_accel_put_mgr_handle);

/**
 * snps_accel_get_mgr_ops - Get operations of a manager.
 * @handle: Handle of a manager.
 *
 * Return: Operations of a manager on success, otherwise an appropriate failure.
 */
const struct snps_accel_mgr_ops *snps_accel_get_mgr_ops(void *handle)
{
	struct snps_accel_mgr *mgr;

	if (unlikely(!handle)) {
		pr_err("%s: Invalid handle.\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mgr = dev_get_drvdata(handle);
	if (unlikely(!mgr)) {
		pr_err("%s: No driver data.\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	dev_info(mgr->dev, "Got operations. handle %p\n", handle);

	return mgr->ops;
}
EXPORT_SYMBOL(snps_accel_get_mgr_ops);

/**
 * __snps_accel_mgr_free - Free a manager instance.
 * @mgr: Manager instance.
 */
static void __snps_accel_mgr_free(struct snps_accel_mgr *mgr)
{
	kfree(mgr);
}

/**
 * __snps_accel_mgr_alloc - Allocate a manager instance.
 * @dev: Device of a manager.
 * @data: Match data.
 * @len: Length of private struct.
 *
 * Return: Manager instance on success, otherwise NULL.
 */
struct snps_accel_mgr *__snps_accel_mgr_alloc(struct device *dev,
					      const void *data,
					      int len)
{
	struct snps_accel_mgr *mgr;

	if (!dev)
		return NULL;

	mgr = kzalloc(sizeof(struct snps_accel_mgr) + len, GFP_KERNEL);
	if (!mgr)
		return NULL;

	mgr->dev = dev;
	mgr->data = data;
	mgr->priv = &mgr[1];

	return mgr;
}

/**
 * snps_accel_mgr_free - Devm free wrapper of a manager instance.
 * @dev: Device of a manager.
 * @res: Manager instance.
 */
static void snps_accel_mgr_free(struct device *dev, void *res)
{
	__snps_accel_mgr_free(*(struct snps_accel_mgr **)res);
}

/**
 * snps_accel_devm_mgr_alloc - Devm allocate wrapper of a manager instance.
 * @dev: Device of a manager.
 * @data: Match data.
 * @len: Length of private struct.
 *
 * Return: Manager instance on success, otherwise NULL.
 */
static struct snps_accel_mgr *
snps_accel_devm_mgr_alloc(struct device *dev, const void *data, int len)
{
	struct snps_accel_mgr **ptr, *mgr;

	ptr = devres_alloc(snps_accel_mgr_free, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	mgr = __snps_accel_mgr_alloc(dev, data, len);
	if (mgr) {
		*ptr = mgr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return mgr;
}

/**
 * snps_accel_mgr_free_tuples_work - Deferred work handler to free tuples.
 * @work: Deferred work to free tuples.
 */
static void snps_accel_mgr_free_tuples_work(struct work_struct *work)
{
	struct snps_accel_mgr_irq *irq =
		container_of(work, struct snps_accel_mgr_irq, free_work);
	struct snps_accel_mgr_irq_tuple *tuple, *next;
	unsigned long flags;
	LIST_HEAD(free_tuples);

	spin_lock_irqsave(&irq->tuples_lock, flags);
	list_replace_init(&irq->free_tuples, &free_tuples);
	spin_unlock_irqrestore(&irq->tuples_lock, flags);

	list_for_each_entry_safe(tuple, next, &free_tuples, irq_link) {
		list_del_init(&tuple->irq_link);
		kfree(tuple);
	}
}

/**
 * snps_accel_arcsync_probe - Probe a manager.
 * @pdev: Platform device.
 * @data: Match data.
 *
 * Return: 0 on success and an appropriate error code otherwise.
 */
int snps_accel_arcsync_probe(struct platform_device *pdev, const void *data)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct snps_accel_mgr *mgr;
	struct snps_accel_arcsync *arcsync;
	struct snps_accel_mgr_irq *irq;
	struct resource *res;
	char *irq_name;
	u32 build_config, num_host_processors = 1;
	int ret, i;

	mgr = snps_accel_devm_mgr_alloc(dev, data,
					sizeof(struct snps_accel_arcsync));
	if (!mgr)
		return -ENOMEM;

	arcsync = mgr->priv;
	arcsync->mgr = mgr;

	/* Properties */
	ret = of_property_read_u32(node, "snps,accel-mgr-id", &mgr->mgr_id);
	if (ret) {
		dev_err(dev, "Failed to read 'snps,accel-mgr-id': %d\n",
			ret);
		return ret;
	}

	mgr->power_always_on =
		of_property_read_bool(node, "snps,power-always-on");

	ret = of_property_read_u32(node, "snps,host-processor-id",
				   &mgr->host_processor_id);
	if (ret) {
		dev_err(dev, "Failed to read 'snps,host-processor-id': %d\n",
			ret);
		return ret;
	}

	ret = of_property_read_u32(node, "snps,host-core-id",
				   &mgr->host_core_id);
	if (ret) {
		mgr->host_core_id = 0;
		dev_warn(dev, "Assume 'snps,host-core-id': %u\n",
			 mgr->host_core_id);
	}

	/* Registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Unable to get reg\n");
		return -EINVAL;
	}

	if (!IS_ALIGNED(resource_size(res), PAGE_SIZE)) {
		dev_err(dev,
			"Invalid size %#zx of reg not aligned to %#lx\n",
			resource_size(res), PAGE_SIZE);
		return -EINVAL;
	}

	mgr->reg_vbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(mgr->reg_vbase)) {
		dev_err(dev, "Failed to get memory region resource\n");
		return PTR_ERR(mgr->reg_vbase);
	}

	spin_lock_init(&mgr->reg_lock);
	mgr->ops = &snps_accel_arcsync_ops;
	mgr->reg_pbase = res->start;
	mgr->reg_size = resource_size(res);

	/* ARCSync initialization */
	build_config = readl(mgr->reg_vbase);
	arcsync->version = build_config & 0xff;
	arcsync->num_processors = ((build_config >> 8) & 0xff) +
				  num_host_processors;
	arcsync->max_cores_per_processor =
			(1U << (((build_config >> 16) & 0x7) + 2));
	arcsync->coreid_bw = ilog2(arcsync->max_cores_per_processor);
	arcsync->has_pmu = (((build_config >> 22) & 0x1) != 0) ? true : false;
	arcsync->max_blocks_per_vpx_processor = ((build_config >> 27) & 0x3);

	dev_info(dev, "Registers: pa %pa sz %#zx va %pS\n",
		 &mgr->reg_pbase, mgr->reg_size, mgr->reg_vbase);
	dev_info(dev, "Host processor ID %u\n", mgr->host_processor_id);
	dev_info(dev, "Host core ID %u\n", mgr->host_core_id);
	dev_info(dev, "ID: %u\n", mgr->mgr_id);
	dev_info(dev, "Type: ARCSync\n");
	dev_info(dev, "Version: %u\n", arcsync->version);
	dev_info(dev, "Number of processors: %u\n", arcsync->num_processors);
	dev_info(dev, "Maximum number of cores per processor: %u\n",
		 arcsync->max_cores_per_processor);
	dev_info(dev, "Bit-width of core ID: %u\n", arcsync->coreid_bw);
	dev_info(dev, "Has PMU: %s\n", arcsync->has_pmu ? "true" : "false");
	dev_info(dev, "Maximum number of blocks per VPX processor: %u\n",
		 arcsync->max_blocks_per_vpx_processor);

	ret = of_property_read_u32(node, "snps,num-vpx-processors",
				   &arcsync->num_vpx_processors);
	if (ret) {
		arcsync->num_vpx_processors = 0;
		dev_warn(dev, "Assume 'snps,num-vpx-processors': %u\n",
			 arcsync->num_vpx_processors);
	}

	if (arcsync->num_vpx_processors >
	    (arcsync->num_processors - num_host_processors)) {
		dev_err(dev,
			"Invalid 'snps,num-vpx-processors': %u\n",
			arcsync->num_vpx_processors);
		return -EINVAL;
	}

	arcsync->num_vpx_blocks = arcsync->num_vpx_processors *
				  arcsync->max_blocks_per_vpx_processor;

	/* Power domain */
	mgr->genpd.name = devm_kasprintf(dev, GFP_KERNEL,
					 "snps_accel_mgr_%u_pd", mgr->mgr_id);
	mgr->genpd.power_off = snps_accel_arcsync_power_off;
	mgr->genpd.power_on = snps_accel_arcsync_power_on;
	ret = pm_genpd_init(&mgr->genpd, NULL, true);
	if (ret) {
		dev_err(dev, "Failed to init power domain %s: %d\n",
			mgr->genpd.name, ret);
		return ret;
	}

	/* Interrupts */
	ret = platform_irq_count(pdev);
	if (ret <= 0 || ret > SNPS_ACCEL_MAX_MGR_IRQS) {
		dev_err(dev, "Invalid number of 'interrupts': %d\n", ret);
		return -EINVAL;
	}

	mgr->num_irqs = ret;

	for (i = 0; i < mgr->num_irqs; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to get IRQ #%d: %d\n",
				i, ret);
			return ret;
		}

		irq = &mgr->irqs[i];
		irq->index = i;
		irq->irq_id = ret;
		irq->mgr = mgr;
		spin_lock_init(&irq->tuples_lock);
		INIT_LIST_HEAD(&irq->tuples);
		irq->num_tuples = 0;
		INIT_WORK(&irq->free_work, snps_accel_mgr_free_tuples_work);
		INIT_LIST_HEAD(&irq->free_tuples);

		irq_name = devm_kasprintf(dev, GFP_KERNEL,
					  "snps_accel_mgr_irq_%u", irq->index);
		ret = devm_request_irq(dev, irq->irq_id,
				       snps_accel_arcsync_irq_base_handler,
				       0, irq_name, irq);
		if (ret) {
			dev_err(dev, "Failed to request IRQ #%u\n",
				irq->index);
			return ret;
		}

		dev_info(dev, "Manager %u requested IRQ #%u (vector ID=%u)\n",
			 mgr->mgr_id, irq->index, irq->irq_id);
	}

	platform_set_drvdata(pdev, mgr);

	dev_info(dev, "Manager %u is probed.\n", mgr->mgr_id);

	return 0;
}

/**
 * snps_accel_arcsync_remove - Remove a manager.
 * @pdev: Platform device.
 */
void snps_accel_arcsync_remove(struct platform_device *pdev)
{
	struct snps_accel_mgr *mgr = platform_get_drvdata(pdev);
	struct snps_accel_mgr_irq *irq;
	struct snps_accel_mgr_irq_tuple *tuple, *next;
	unsigned long flags;
	int i;
	LIST_HEAD(free_tuples);

	for (i = 0; i < mgr->num_irqs; i++) {
		irq = &mgr->irqs[i];

		spin_lock_irqsave(&irq->tuples_lock, flags);
		list_for_each_entry_safe(tuple, next, &irq->tuples, irq_link) {
			list_del_init(&tuple->irq_link);
			irq->num_tuples--;
			if (refcount_dec_and_test(&tuple->refcount))
				list_add_tail(&tuple->irq_link, &free_tuples);
		}
		list_splice_init(&irq->free_tuples, &free_tuples);
		spin_unlock_irqrestore(&irq->tuples_lock, flags);

		cancel_work_sync(&irq->free_work);

		list_for_each_entry_safe(tuple, next, &free_tuples, irq_link) {
			list_del_init(&tuple->irq_link);
			kfree(tuple);
		}
	}

	dev_info(mgr->dev, "Manager %u is removed.\n", mgr->mgr_id);
}
