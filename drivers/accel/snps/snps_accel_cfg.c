// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include "snps_accel_drv.h"

/* DMI-CFG offsets */
#define SNPS_ACCEL_NPX_GROUP_OFF		0x20000
#define SNPS_ACCEL_NPX_GROUP_DBANK_ECC_CTRL_OFF(group_id)	\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x24)
#define SNPS_ACCEL_NPX_GROUP_AXI_TOP_OFF(group_id)		\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x1000)
#define SNPS_ACCEL_NPX_GROUP_AXI_BOT_OFF(group_id)		\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x2000)
#define SNPS_ACCEL_NPX_GROUP_CSM_REMAP_OFF(group_id)		\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x3000)
#define SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE	0x2000
#define SNPS_ACCEL_NPX_GROUP_FS_CTRL_OFF(group_id, idx)		\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x4000 +	\
	 (idx) * SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE)
#define SNPS_ACCEL_NPX_GROUP_CCM_DEMUX_OFF(group_id)		\
	((group_id) * SNPS_ACCEL_NPX_GROUP_OFF + 0x10000)

#define SNPS_ACCEL_NPX_L2_AXI_OFF		0x80000
#define SNPS_ACCEL_NPX_L2_CBU_OFF		0x81000
#define SNPS_ACCEL_NPX_L2_REMAP_OFF		0x82000

#define SNPS_ACCEL_NPX_DECBASE_OFF		0x0
#define SNPS_ACCEL_NPX_DECSIZE_OFF		0x80
#define SNPS_ACCEL_NPX_DECMST_OFF		0x100

/* DMI-related device (physical) addresses */
#define SNPS_ACCEL_NPX_DMI_MST			0

#define SNPS_ACCEL_NPX_L1_PERIPH_SIZE		0x400000
#define SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE	\
	(SNPS_ACCEL_NPX_L1_PERIPH_SIZE * SNPS_ACCEL_NPX_MAX_CORES_PG)
#define SNPS_ACCEL_NPX_GROUP_L1_PERIPH_BASE(base_da, group_id)	\
	((base_da) + SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE * (group_id))

#define SNPS_ACCEL_NPX_L1_STU_SIZE		0x1000
#define SNPS_ACCEL_NPX_NUM_STU_PG		2
#define SNPS_ACCEL_NPX_GROUP_STU_SIZE	\
	(SNPS_ACCEL_NPX_L1_STU_SIZE * SNPS_ACCEL_NPX_NUM_STU_PG)
#define SNPS_ACCEL_NPX_GROUP_STU_BASE(base_da, group_id)	\
	((base_da) + 0x6080000 + SNPS_ACCEL_NPX_GROUP_STU_SIZE * (group_id))

#define SNPS_ACCEL_NPX_GROUP_FS_CTRL_BASE(base_da, group_id, idx)	\
	((base_da) + (group_id) * SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE +	\
	(idx) * SNPS_ACCEL_NPX_L1_PERIPH_SIZE + 0x84000)

#define SNPS_ACCEL_NPX_L2_DCCM_BASE(base_da)	((base_da) + 0x6000000)
#define SNPS_ACCEL_NPX_L2_DCCM_SIZE		0x80000

#define SNPS_ACCEL_NPX_L2_CFG_AXI_BASE(base_da)	((base_da) + 0x6400000)
#define SNPS_ACCEL_NPX_L2_CFG_AXI_SIZE		0x100000
#define SNPS_ACCEL_NPX_L2_CFG_AXI_MST		2

#define SNPS_ACCEL_NPX_GROUP_FS_CTRL_MAP_BASE(base_da, group_id, idx)	\
	(SNPS_ACCEL_NPX_L2_CFG_AXI_BASE((base_da)) +	\
	 SNPS_ACCEL_NPX_GROUP_FS_CTRL_OFF((group_id), (idx)))

#define SNPS_ACCEL_NPX_CSM_BASE(base_da)	((base_da) + 0x8000000)
#define SNPS_ACCEL_NPX_CSM_MST			0
#define SNPS_ACCEL_NPX_CSM_BANKS_PG		8U
#define SNPS_ACCEL_NPX_GROUP_CSM_IL_BW(lsb)	\
	(ilog2(SNPS_ACCEL_NPX_CSM_BANKS_PG << (lsb)))

/**
 * snps_accel_npx_v2_cfg_ap - Configure aperture.
 * @addr: Host virtual address of DMI CFG register.
 * @ap_idx: Index of aperture.
 * @base: Base address of aperture.
 * @size: Size of aperture.
 * @mst: Master port number of aperture.
 * @lsb: Left-shift-bits used for @base and @size.
 */
static void snps_accel_npx_v2_cfg_ap(void __iomem *addr, int ap_idx, u32 base,
				     const u32 size, u32 mst, u32 lsb)
{
	u32 base_value = base >> lsb;
	u32 size_value = ~(size - 1) >> lsb;

	writel(base_value, addr + SNPS_ACCEL_NPX_DECBASE_OFF + ap_idx * 4);
	writel(size_value, addr + SNPS_ACCEL_NPX_DECSIZE_OFF + ap_idx * 4);
	writel(mst, addr + SNPS_ACCEL_NPX_DECMST_OFF + ap_idx * 4);
}

/**
 * snps_accel_npx_v2_cfg_ap_di - Configure aperture with a direct size mask.
 * @addr: Host virtual address of DMI CFG register.
 * @ap_idx: Index of aperture.
 * @base: Base address of aperture.
 * @size_mask: Size mask of aperture.
 * @mst: Master port number of aperture.
 * @lsb: Left-shift-bits used for @base and @size.
 */
static void
snps_accel_npx_v2_cfg_ap_di(void __iomem *addr, int ap_idx, u32 base,
			    const u32 size_mask, u32 mst, u32 lsb)
{
	u32 base_value = base >> lsb;
	u32 size_value = size_mask >> lsb;

	writel(base_value, addr + SNPS_ACCEL_NPX_DECBASE_OFF + ap_idx * 4);
	writel(size_value, addr + SNPS_ACCEL_NPX_DECSIZE_OFF + ap_idx * 4);
	writel(mst, addr + SNPS_ACCEL_NPX_DECMST_OFF + ap_idx * 4);
}

/**
 * snps_accel_npx_v2_cfg_csmremap_ap - Configure aperture of CSM remap.
 * @addr: Host virtual address of DMI CFG register.
 * @ap_idx: Index of aperture.
 * @num_groups: Num of groups.
 */
static void
snps_accel_npx_v2_cfg_csmremap_ap(void __iomem *addr, int ap_idx,
				  u32 num_groups)
{
	u32 drop_bw;

	if (num_groups == 1)
		drop_bw = 0;
	else if (num_groups == 2)
		drop_bw = 1;
	else if (num_groups == 4)
		drop_bw = 2;
	else if (num_groups == 8)
		drop_bw = 3;
	else
		drop_bw = 0;

	writel(drop_bw, addr + SNPS_ACCEL_NPX_DECBASE_OFF + ap_idx * 4);
}

/**
 * snps_accel_npx_v2_cfg_remap_ap - Configure aperture of remap.
 * @addr: Host virtual address of DMI CFG register.
 * @ap_idx: Index of aperture.
 * @ap1_base: Base address of aperture 1.
 * @ap1_size: Size of aperture 1.
 * @ap2_base: Base address of aperture 2.
 * @ap2_size: Size of aperture 2.
 * @ap2_lsb: Left-shift-bits used for @ap2_size.
 */
static void snps_accel_npx_v2_cfg_remap_ap(void __iomem *addr, int ap_idx,
					   const phys_addr_t ap1_base,
					   const u32 ap1_size,
					   const phys_addr_t ap2_base,
					   const u32 ap2_size,
					   const u32 ap2_lsb)
{
	u32 base1 = ap1_base >> 12;
	u32 size1 = ~(ap1_size - 1) >> 12;
	u32 base2 = ap2_base >> 12;
	u32 size2 = ~(ap2_size - 1) >> 12;

	size1 = size1 & ((1U << (40 - 12)) - 1);
	writel(base1, addr + SNPS_ACCEL_NPX_DECBASE_OFF + ap_idx * 4);
	writel(size1, addr + SNPS_ACCEL_NPX_DECSIZE_OFF + ap_idx * 4);

	size2 = size2 & ((1U << (ap2_lsb - 12)) - 1);
	writel(base2, addr + SNPS_ACCEL_NPX_DECBASE_OFF + (ap_idx + 1) * 4);
	writel(size2, addr + SNPS_ACCEL_NPX_DECSIZE_OFF + (ap_idx + 1) * 4);
}

/**
 * snps_accel_npx_v2_cfg_core_group_remap - Configure NPX group remap.
 * @group: NPX group.
 */
static void
snps_accel_npx_v2_cfg_core_group_remap(struct snps_accel_core_group *group)
{
	struct snps_accel_processor *proc = group->proc;
	struct snps_accel_npx *npx = proc->priv;
	void __iomem *dmi_cfg_vbase, *dmi_cfg_vaddr;
	u32 num_l1_cores_pg, lsb, i, dmi_base_da_u32, caddr, saddr;
	u64 caddr_base_da_u64, dmi_base_da_u64;
	int ap_idx, ret;

	dmi_cfg_vbase = snps_accel_get_npx_dmi_cfg_vaddr(proc);
	if (!dmi_cfg_vbase)
		return;

	ret = snps_accel_get_mem_daddr(proc, SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI,
				       &dmi_base_da_u64);
	BUG_ON(ret || dmi_base_da_u64 > U32_MAX);
	dmi_base_da_u32 = (u32)dmi_base_da_u64;

	/* Config cln1p0 remap */
	dmi_cfg_vaddr = dmi_cfg_vbase +
			SNPS_ACCEL_NPX_GROUP_CSM_REMAP_OFF(group->group_id);
	ap_idx = 0;

	snps_accel_npx_v2_cfg_csmremap_ap(dmi_cfg_vaddr, ap_idx,
					  npx->num_groups);
	ap_idx += 2;

	dev_dbg(&proc->pdev->dev,
		"NPX core group %u_%u_%u remap is configured.\n",
		proc->system->system_id, proc->processor_id, group->group_id);

	if (proc->safety_level == 0)
		return;

	/* Config FS register remap */
	num_l1_cores_pg = npx->num_l1_cores / npx->num_groups;
	lsb = ilog2(SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE);
	for (i = 0; i < num_l1_cores_pg; i++) {
		/* Remap dmi_cfg address */
		ret = snps_accel_get_mem_daddr(proc,
					SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI_CFG,
					&caddr_base_da_u64);
		BUG_ON(ret || caddr_base_da_u64 > U32_MAX);
		caddr =	(u32)caddr_base_da_u64 +
			SNPS_ACCEL_NPX_GROUP_FS_CTRL_OFF(group->group_id, i);
		saddr = SNPS_ACCEL_NPX_GROUP_FS_CTRL_BASE(dmi_base_da_u32,
							  group->group_id, i);
		snps_accel_npx_v2_cfg_remap_ap(dmi_cfg_vaddr, ap_idx, caddr,
			SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE,
			saddr, SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE, lsb);
		ap_idx += 2;

		/* L2 Safety Remap */
		caddr = SNPS_ACCEL_NPX_GROUP_FS_CTRL_MAP_BASE(
			dmi_base_da_u32, group->group_id, i);
		saddr = SNPS_ACCEL_NPX_GROUP_FS_CTRL_BASE(
			dmi_base_da_u32, group->group_id, i);
		snps_accel_npx_v2_cfg_remap_ap(dmi_cfg_vaddr, ap_idx,
			caddr, SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE,
			saddr, SNPS_ACCEL_NPX_GROUP_FS_CTRL_SIZE, lsb);
		ap_idx += 2;
	}

	dev_dbg(&proc->pdev->dev,
		"NPX core group %u_%u_%u FS is configured.\n",
		proc->system->system_id, proc->processor_id, group->group_id);
}

/**
 * struct snps_accel_msts_map - Map of AXI master port numbers.
 * @msts: AXI master port numbers.
 */
struct snps_accel_msts_map {
	u32 msts[SNPS_ACCEL_NPX_MAX_GROUPS];
};

/**
 * snps_accel_npx_v2_cfg_core_group_conn - Configure NPX group interconnection.
 * @group: Target NPX group.
 */
static void
snps_accel_npx_v2_cfg_core_group_conn(struct snps_accel_core_group *group)
{
	struct snps_accel_processor *proc = group->proc;
	struct snps_accel_npx *npx = proc->priv;
	struct snps_accel_msts_map *msts_map;
	u64 dmi_base_da_u64;
	u32 addr, dmi_base_da_u32, csm_size_mask,
	    num_l1_cores_pg, i, lsb, csm_lsb;
	void __iomem *dmi_cfg_vbase, *dmi_cfg_vaddr;
	int mst, ap_idx, ret;
	struct snps_accel_msts_map msts_map_v2p0[SNPS_ACCEL_NPX_MAX_GROUPS] = {
		{.msts = {1, 2, 3, 4}},
		{.msts = {2, 1, 4, 3}},
		{.msts = {3, 4, 1, 2}},
		{.msts = {4, 3, 2, 1}},
	};
	struct snps_accel_msts_map msts_map_v2p1[SNPS_ACCEL_NPX_MAX_GROUPS] = {
		{.msts = {0, 1, 2, 3}},
		{.msts = {1, 0, 3, 2}},
		{.msts = {2, 3, 0, 1}},
		{.msts = {3, 2, 1, 0}},
	};

	dmi_cfg_vbase = snps_accel_get_npx_dmi_cfg_vaddr(proc);
	if (!dmi_cfg_vbase)
		return;

	ret = snps_accel_get_mem_daddr(proc, SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI,
				       &dmi_base_da_u64);
	BUG_ON(ret || dmi_base_da_u64 > U32_MAX);
	dmi_base_da_u32 = (u32)dmi_base_da_u64;

	lsb = 12;
	if (snps_accel_npx_v2p0(proc)) {
		msts_map = msts_map_v2p0;
		csm_lsb = 12;
	} else {
		msts_map = msts_map_v2p1;
		csm_lsb = 10;
	}

	/* Config core group top AXI matrix */
	dmi_cfg_vaddr = dmi_cfg_vbase +
			SNPS_ACCEL_NPX_GROUP_AXI_TOP_OFF(group->group_id);
	ap_idx = 0;

	/* L1 peripheral of a core group */
	for (i = 0; i < npx->num_groups; i++)
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_GROUP_L1_PERIPH_BASE(dmi_base_da_u32,
							    i),
			SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE,
			msts_map[group->group_id].msts[i], lsb);

	/* STU MMIO of a core group */
	for (i = 0; i < npx->num_groups; i++)
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_GROUP_STU_BASE(dmi_base_da_u32, i),
			SNPS_ACCEL_NPX_GROUP_STU_SIZE,
			msts_map[group->group_id].msts[i], lsb);

	/* CSM of a core group */
	csm_size_mask = ~((proc->csm_size_mb << 20) - 1) |
		((npx->num_groups - 1U) <<
		 SNPS_ACCEL_NPX_GROUP_CSM_IL_BW(csm_lsb));
	for (i = 0; i < npx->num_groups; i++) {
		addr = SNPS_ACCEL_NPX_CSM_BASE(dmi_base_da_u32) |
		       (i << SNPS_ACCEL_NPX_GROUP_CSM_IL_BW(csm_lsb));
		snps_accel_npx_v2_cfg_ap_di(dmi_cfg_vaddr, ap_idx++,
					    addr, csm_size_mask,
					    msts_map[group->group_id].msts[i],
					    lsb);
	}

	/* L2 core(s) DCCM */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
		SNPS_ACCEL_NPX_L2_DCCM_BASE(dmi_base_da_u32),
		SNPS_ACCEL_NPX_L2_DCCM_SIZE,
		snps_accel_npx_v2p0(proc) ? 1 : 0, lsb);

	if (snps_accel_npx_v2p0(proc)) {
		/* Others routes to port 0 */
		ap_idx = 14;
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx, 0x0, 0x0,
					 0, lsb);
	}

	/* Config core group bottom AXI matrix */
	dmi_cfg_vaddr = dmi_cfg_vbase +
			SNPS_ACCEL_NPX_GROUP_AXI_BOT_OFF(group->group_id);
	ap_idx = 1;

	/* Access CSM banks */
	csm_size_mask = ~((proc->csm_size_mb << 20) - 1) |
			((SNPS_ACCEL_NPX_CSM_BANKS_PG - 1) << csm_lsb);
	for (i = 0; i < SNPS_ACCEL_NPX_CSM_BANKS_PG; i++) {
		addr = SNPS_ACCEL_NPX_CSM_BASE(dmi_base_da_u32) |
		       (i << csm_lsb);
		snps_accel_npx_v2_cfg_ap_di(dmi_cfg_vaddr, ap_idx++,
					    addr, csm_size_mask, i, csm_lsb);
	}

	/* Group-local L1 peripheral */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
		SNPS_ACCEL_NPX_GROUP_L1_PERIPH_BASE(dmi_base_da_u32,
						    group->group_id),
		SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE,
		SNPS_ACCEL_NPX_CSM_BANKS_PG, csm_lsb);

	/* Group-local L2 DCCM */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
				 SNPS_ACCEL_NPX_L2_DCCM_BASE(dmi_base_da_u32),
				 SNPS_ACCEL_NPX_L2_DCCM_SIZE,
				 SNPS_ACCEL_NPX_CSM_BANKS_PG, csm_lsb);

	/* STU MMIO */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
		SNPS_ACCEL_NPX_GROUP_STU_BASE(dmi_base_da_u32, group->group_id),
		SNPS_ACCEL_NPX_GROUP_STU_SIZE, SNPS_ACCEL_NPX_CSM_BANKS_PG,
		csm_lsb);

	/* Config ccm_demux of the group */
	dmi_cfg_vaddr = dmi_cfg_vbase +
			SNPS_ACCEL_NPX_GROUP_CCM_DEMUX_OFF(group->group_id);
	ap_idx = 0;
	mst = 0;
	num_l1_cores_pg = npx->num_l1_cores / npx->num_groups;

	/* Group-local L1 peripheral */
	for (i = 0; i < num_l1_cores_pg; i++) {
		addr = SNPS_ACCEL_NPX_GROUP_L1_PERIPH_BASE(dmi_base_da_u32,
							   group->group_id) +
		       i * SNPS_ACCEL_NPX_L1_PERIPH_SIZE;
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++, addr,
					 SNPS_ACCEL_NPX_L1_PERIPH_SIZE, mst++,
					 lsb);
	}

	/* Group-local STU(s) MMIO */
	for (i = 0; i < SNPS_ACCEL_NPX_NUM_STU_PG; i++) {
		addr = SNPS_ACCEL_NPX_GROUP_STU_BASE(dmi_base_da_u32,
						     group->group_id) +
		       i * SNPS_ACCEL_NPX_L1_STU_SIZE;
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++, addr,
					 SNPS_ACCEL_NPX_L1_STU_SIZE, mst++,
					 lsb);
	}

	/* Access L2 core(s) DCCM */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
				 SNPS_ACCEL_NPX_L2_DCCM_BASE(dmi_base_da_u32),
				 SNPS_ACCEL_NPX_L2_DCCM_SIZE, mst++, lsb);

	dev_dbg(&proc->pdev->dev,
		"NPX core group %u_%u_%u interconnect is configured.\n",
		proc->system->system_id, proc->processor_id, group->group_id);
}

/**
 * snps_accel_npx_v2_cfg_l2_core_group - Configure NPX v2 L2 core group.
 * @proc: Target NPX processor.
 */
static void
snps_accel_npx_v2_cfg_l2_core_group(struct snps_accel_processor *proc)
{
	struct snps_accel_npx *npx = proc->priv;
	void __iomem *dmi_cfg_vbase, *dmi_cfg_vaddr;
	int ret, ap_idx, csm_lsb, lsb = 12;
	u64 dmi_base_da_u64;
	u32 csm_base_da, dmi_base_da_u32, dmi_size_u32, csm_size_mask, i;

	dmi_cfg_vbase = snps_accel_get_npx_dmi_cfg_vaddr(proc);
	if (!dmi_cfg_vbase)
		return;

	ret = snps_accel_get_mem_daddr(proc, SNPS_ACCEL_PROC_MEM_TYPE_NPX_DMI,
				       &dmi_base_da_u64);
	BUG_ON(ret || dmi_base_da_u64 > U32_MAX);
	dmi_base_da_u32 = (u32)dmi_base_da_u64;
	csm_lsb = snps_accel_npx_v2p0(proc) ? 12 : 10;

	/* AXI Matrix */
	dmi_cfg_vaddr = dmi_cfg_vbase + SNPS_ACCEL_NPX_L2_AXI_OFF;
	ap_idx = 0;

	/* L2 core(s) DCCM */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
				 SNPS_ACCEL_NPX_L2_DCCM_BASE(dmi_base_da_u32),
				 SNPS_ACCEL_NPX_L2_DCCM_SIZE,
				 npx->num_groups, lsb);

	for (i = 0; i < npx->num_groups; i++) {
		/* L1 peripheral of a core group */
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_GROUP_L1_PERIPH_BASE(dmi_base_da_u32,
							    i),
			SNPS_ACCEL_NPX_GROUP_L1_PERIPH_SIZE, i, lsb);

		/* STU MMIO of a core group */
		snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_GROUP_STU_BASE(dmi_base_da_u32, i),
			SNPS_ACCEL_NPX_GROUP_STU_SIZE, i, lsb);
	}

	/* Bypass L1 peripheral & STU MMIO of non-existing core group(s) */
	ap_idx += (SNPS_ACCEL_NPX_MAX_GROUPS - npx->num_groups) * 2;

	/* CSM of group(s) */
	csm_size_mask = ~((proc->csm_size_mb << 20) - 1) |
		((npx->num_groups - 1) <<
		 SNPS_ACCEL_NPX_GROUP_CSM_IL_BW(csm_lsb));
	for (i = 0; i < npx->num_groups; i++) {
		csm_base_da = SNPS_ACCEL_NPX_CSM_BASE(dmi_base_da_u32) |
			      (i << SNPS_ACCEL_NPX_GROUP_CSM_IL_BW(csm_lsb));
		snps_accel_npx_v2_cfg_ap_di(dmi_cfg_vaddr, ap_idx++,
					    csm_base_da, csm_size_mask, i,
					    lsb);
	}

	/* CBU Matrix */
	dmi_cfg_vaddr = dmi_cfg_vbase + SNPS_ACCEL_NPX_L2_CBU_OFF;
	ap_idx = 0;

	/* L2 core(s) access CFG AXI Matrix */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_L2_CFG_AXI_BASE(dmi_base_da_u32),
			SNPS_ACCEL_NPX_L2_CFG_AXI_SIZE,
			SNPS_ACCEL_NPX_L2_CFG_AXI_MST, lsb);

	/* L2 core(s) access all peripherals */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
		dmi_base_da_u32, dmi_size_u32, SNPS_ACCEL_NPX_DMI_MST, lsb);

	/* L2 core(s) access CSM */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++,
			SNPS_ACCEL_NPX_CSM_BASE(dmi_base_da_u32),
			proc->csm_size_mb << 20, SNPS_ACCEL_NPX_CSM_MST, lsb);

	/* L2 core(s) access L2 NoC port */
	snps_accel_npx_v2_cfg_ap(dmi_cfg_vaddr, ap_idx++, 0, 0, 1, lsb);

	dev_dbg(&proc->pdev->dev, "NPX L2 group %u_%u is configured.\n",
		proc->system->system_id, proc->processor_id);
}

/**
 * snps_accel_npx_cfg_core_group - Configure NPX core group.
 * @group: Target NPX group.
 */
void snps_accel_npx_cfg_core_group(struct snps_accel_core_group *group)
{
	if (snps_accel_npx_v2(group->proc) && (group->proc->num_cores > 1)) {
		snps_accel_npx_v2_cfg_core_group_conn(group);
		snps_accel_npx_v2_cfg_core_group_remap(group);
	}
}

/**
 * snps_accel_npx_cfg_l2_core_group - Configure NPX L2 core group.
 * @proc: Target NPX processor.
 */
void snps_accel_npx_cfg_l2_core_group(struct snps_accel_processor *proc)
{
	if (snps_accel_npx_v2(proc) && (proc->num_cores > 1))
		snps_accel_npx_v2_cfg_l2_core_group(proc);
}
