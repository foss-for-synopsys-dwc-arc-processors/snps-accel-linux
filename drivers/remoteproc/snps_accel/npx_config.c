// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys VPX/NPX remoteporc driver helper
 * Configure NPX Cluster Network memory map
 *
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/log2.h>

#include "accel_rproc.h"

#define NPX_COREID_L2C0				0x00
#define NPX_COREID_L1C0				0x01

/* CFG MMIO */
#define NPX_CFG_DECBASE				0x000
#define NPX_CFG_DECSIZE				0x080
#define NPX_CFG_DECMST				0x100

/* The driver implements recommended CLN map setup according to the NPX Databook:
 * NPX Memory map section. The driver doesn't not allow change regions
 * parameters inside the map, but it creates a memory map based on several
 * DTS properties:
 *     snps,npu-slice-num - number of slices
 *     snps,cln-map-start - start address in the CLN address space for DMI mappings.
 *     snps,csm-size - size of L2 CSM memory
 *     snps,csm-banks-per-group - CSM banks per group
 *     snps,stu-per-group - number of STUs per group
 *     snps,cln-safety-lvl - functional Safety support, the driver remaps safety MMIO
 *     snps,cln-revision - Cluster Network revision (0 - CLN1.0, 1 - CLN1.5). Selects
 *                         the CLN port shuffle map. Default 0 (CLN1.0).
 *     snps,csm-bank-lsb - log2 of the CSM bank granularity and bottom-matrix local
 *                         aperture decode shift. Default 12 (CLN1.0) or 10 (CLN1.5).
 *     snps,csm-window - fixed CSM address-decode window in the CLN map used to
 *                       build the CSM region masks. Default 0x4000000 (64M).
 *     snps,l2-dccm-size - size of the L2 DCCM window. Default 0x80000 (512K).
 */

/* CLN config MMIO */
#define NPX_CFG_L1_GRP_OFFSET			0x20000
#define NPX_CFG_L1_GRP_AXI_TOP(grp)		((grp) * NPX_CFG_L1_GRP_OFFSET + 0x1000)
#define NPX_CFG_L1_GRP_AXI_BOTTOM(grp)		((grp) * NPX_CFG_L1_GRP_OFFSET + 0x2000)
#define NPX_CFG_L1_CSM_REMAP(grp)		((grp) * NPX_CFG_L1_GRP_OFFSET + 0x3000)
#define NPX_CFG_L1_GRP_CCM_DEMUX(grp)		((grp) * NPX_CFG_L1_GRP_OFFSET + 0x10000)

#define NPX_CFG_L2_AXI_MATRIX			0x80000
#define NPX_CFG_L2_CBU_MATRIX			0x81000

/* CLN Map config */
#define NPX_CLN_MAP_START			(cn->map_start)

/* L2 Mem map config */
#define NPX_CBU_L2_PERIPH_ADDR			(NPX_CLN_MAP_START + 0)
#define NPX_CBU_L2_PERIPH_SIZE			0x10000000
#define NPX_CBU_L2_PERIPH_PORT			0
#define NPX_CBU_L2_CFG_AXI_ADDR			(NPX_CLN_MAP_START + 0x6400000)
#define NPX_CBU_L2_CFG_AXI_SIZE			0x100000
#define NPX_CBU_L2_CFG_AXI_PORT			2
#define NPX_CBU_L2_CSM_ADDR			(NPX_CLN_MAP_START + 0x8000000)
#define NPX_CBU_L2_CSM_SIZE			0x100000
#define NPX_CBU_L2_CSM_PORT			0
#define NPX_CBU_L2_NOC_PORT			1

/* CLN map config */
#define NPX_CLN_L1_GRP_PERIPH_ADDR(grp)		(NPX_CLN_MAP_START + 0x1000000 * (grp))
#define NPX_CLN_L1_GRP_PERIPH_SIZE		0x1000000
#define NPX_CLN_L2_DCCM_ADDR			(NPX_CLN_MAP_START + 0x6000000)
#define NPX_CLN_L2_DCCM_SIZE			0x80000
#define NPX_CLN_STU_ADDR			(NPX_CLN_MAP_START + 0x6080000)
#define NPX_CLN_L1_GRP_STU_ADDR(grp)		(NPX_CLN_STU_ADDR + 0x2000 * (grp))
#define NPX_CLN_L1_GRP_STU_SIZE			0x2000
#define NPX_CLN_L1_GRP_SFTY(grp, slice)		(NPX_CLN_MAP_START + (grp) * 0x1000000 + \
						 (slice) * 0x400000 + 0x84000)

/* CSM map config */
#define NPX_CLN_CSM_ADDR			(NPX_CLN_MAP_START + 0x8000000)
#define NPX_CLN_CSM_SIZE			(cn->csm_size)

#define NPX_CLN_CSM_BANKS_PER_GRP		(cn->csm_banks_per_grp)
#define NPX_CLN_CSM_GRP_BANK_GRANUL		(1U << (cn)->csm_bank_lsb)
#define NPX_CLN_CSM_GRP_INTERLEAVING		(NPX_CLN_CSM_BANKS_PER_GRP * \
						 NPX_CLN_CSM_GRP_BANK_GRANUL)
#define NPX_CLN_CSM_GRP_ADDR(grp)		(NPX_CLN_CSM_ADDR + \
						 NPX_CLN_CSM_GRP_INTERLEAVING * (grp))
#define NPX_CLN_CSM_GRP_BANK_ADDR(b)		(NPX_CLN_CSM_ADDR + \
						 NPX_CLN_CSM_GRP_BANK_GRANUL * (b))
#define NPX_CLN_L1_SLICE_PERIPH_SIZE		0x400000
#define NPX_CLN_L1_STU_SIZE			0x1000

/* Safety regs remap */
#define NPX_CLN_L1_GRP_SFTY_SIZE		0x2000

#define NPX_CLN_MAX_GROUPS			4

#define NPX_CLN_ADDR_WIDTH			40
#define NPX_CLN_ADDR_MASK			GENMASK_ULL(NPX_CLN_ADDR_WIDTH - 1, 0)

/*
 *
 * Group connections are hardwired to certain ports according to the outgoing
 * shuffle table. The table supports configurations with 1, 2, 4 groups.
 * Four groups connect example for CLN 1.5, where the self-group is connected
 * to port 0:
 *
 *   GR0 ----1--->|----2--->|----3--->|
 *               GR1       GR2       GR3
 *
 *   GR1 ----1--->|----3--->|----2--->|
 *               GR0       GR2       GR3
 *
 *   GR2 ----2--->|----3--->|----1--->|
 *               GR0       GR1       GR3
 *
 *   GR3 ----3--->|----2--->|----1--->|
 *               GR0       GR1       GR2
 *
 * Port shuffle tables: msts_map[src_grp][tgt_grp] = port number.
 * v1r0: all groups including self, ports start at 1.
 * v1r5: self-group maps to port 0 (bottom matrix).
 */
static const u32 msts_map_v1r0[NPX_CLN_MAX_GROUPS][NPX_CLN_MAX_GROUPS] = {
	{1, 2, 3, 4},
	{2, 1, 4, 3},
	{3, 4, 1, 2},
	{4, 3, 2, 1},
};

static const u32 msts_map_v1r5[NPX_CLN_MAX_GROUPS][NPX_CLN_MAX_GROUPS] = {
	{0, 1, 2, 3},
	{1, 0, 3, 2},
	{2, 3, 0, 1},
	{3, 2, 1, 0},
};

static inline u32 npx_dec_size(u64 size_mask, u32 lsb)
{
	return (u32)((size_mask & NPX_CLN_ADDR_MASK) >> lsb);
}

static inline u64 npx_size_mask(u32 apsize)
{
	return ~((u64)apsize - 1) & NPX_CLN_ADDR_MASK;
}

static void
npx_config_aperture(void __iomem *ptr, int apidx, phys_addr_t apbase, const u32 apsize, int mst)
{
	phys_addr_t base = apbase >> 12;
	u32 size = npx_dec_size(npx_size_mask(apsize), 12);

	writel(base, ptr + NPX_CFG_DECBASE + apidx * 4);
	writel(size, ptr + NPX_CFG_DECSIZE + apidx * 4);
	writel(mst, ptr + NPX_CFG_DECMST + apidx * 4);
}

static void
npx_config_aperture_lsb(void __iomem *ptr, int apidx, phys_addr_t apbase,
			const u32 apsize, int mst, u32 lsb)
{
	u32 base = apbase >> lsb;
	u32 size = npx_dec_size(npx_size_mask(apsize), lsb);

	writel(base, ptr + NPX_CFG_DECBASE + apidx * 4);
	writel(size, ptr + NPX_CFG_DECSIZE + apidx * 4);
	writel(mst, ptr + NPX_CFG_DECMST + apidx * 4);
}

static void
npx_config_aperture_direct(void __iomem *ptr, int apidx, phys_addr_t apbase,
			   const u64 size_mask, int mst, u32 lsb)
{
	u32 base = apbase >> lsb;
	u32 size = npx_dec_size(size_mask, lsb);

	writel(base, ptr + NPX_CFG_DECBASE + apidx * 4);
	writel(size, ptr + NPX_CFG_DECSIZE + apidx * 4);
	writel(mst, ptr + NPX_CFG_DECMST + apidx * 4);
}

static void npx_config_l2_grp(void __iomem *cfg_ptr, struct snps_npu_cn *cn)
{
	void __iomem *l2_cfg;
	u64 csm_size_mask;
	u32 il_bw, csm_base;
	int gr;
	int apidx = 0;

	/* Config L2 AXI matrix */
	l2_cfg = cfg_ptr + NPX_CFG_L2_AXI_MATRIX;

	/* L2 DCCM */
	npx_config_aperture(l2_cfg, apidx++, NPX_CLN_L2_DCCM_ADDR,
			    cn->l2_dccm_size, cn->num_grps);

	for (gr = 0; gr < cn->num_grps; gr++) {
		/* L1 slice peripheral aperture */
		npx_config_aperture(l2_cfg, apidx++,
				    NPX_CLN_L1_GRP_PERIPH_ADDR(gr),
				    NPX_CLN_L1_GRP_PERIPH_SIZE, gr);
		/* STU MMIO aperture */
		npx_config_aperture(l2_cfg, apidx++,
				    NPX_CLN_L1_GRP_STU_ADDR(gr),
				    NPX_CLN_L1_GRP_STU_SIZE, gr);
	}

	/* CSM with group interleaving mask */
	il_bw = ilog2(cn->csm_banks_per_grp << cn->csm_bank_lsb);
	csm_size_mask = npx_size_mask(cn->csm_window) |
			((u64)(cn->num_grps - 1) << il_bw);
	for (gr = 0; gr < cn->num_grps; gr++) {
		csm_base = NPX_CLN_CSM_ADDR | (gr << il_bw);
		npx_config_aperture_direct(l2_cfg, apidx++,
					   csm_base, csm_size_mask, gr, 12);
	}

	/* Config CBU matrix */
	apidx = 0;
	l2_cfg = cfg_ptr + NPX_CFG_L2_CBU_MATRIX;

	if (cn->revision == CLN_REVISION_V1R5) {
		/* L2 access CFG AXI Matrix -> port 2 */
		npx_config_aperture(l2_cfg, apidx++, NPX_CBU_L2_CFG_AXI_ADDR,
				    NPX_CBU_L2_CFG_AXI_SIZE,
				    NPX_CBU_L2_CFG_AXI_PORT);
	}

	/* L2 access peripheral -> port 0 to top_matrix */
	npx_config_aperture(l2_cfg, apidx++, NPX_CBU_L2_PERIPH_ADDR,
			    NPX_CBU_L2_PERIPH_SIZE, NPX_CBU_L2_PERIPH_PORT);

	if (cn->revision == CLN_REVISION_V1R5) {
		/* L2 access CSM -> port 0 to top_matrix */
		npx_config_aperture(l2_cfg, apidx++, NPX_CBU_L2_CSM_ADDR,
				    cn->csm_window, NPX_CBU_L2_CSM_PORT);
	}

	/* L2 access L2 NoC port -> port 1 */
	npx_config_aperture(l2_cfg, apidx++, 0, 0, NPX_CBU_L2_NOC_PORT);
}

static void
npx_config_cln_grp(void __iomem *cfg_ptr, struct snps_npu_cn *cn, u32 gr)
{
	const u32 (*msts_map)[NPX_CLN_MAX_GROUPS];
	void __iomem *cfg_dmi;
	u64 csm_size_mask, csm_bank_mask;
	u32 il_bw, csm_base;
	int apidx = 0;
	int port = 0;
	int i;

	msts_map = (cn->revision == CLN_REVISION_V1R0) ?
		   msts_map_v1r0 : msts_map_v1r5;

	/* Config L1 group top AXI matrix */
	cfg_dmi = cfg_ptr + NPX_CFG_L1_GRP_AXI_TOP(gr);

	/* Slice peripheral */
	for (i = 0; i < cn->num_grps; i++)
		npx_config_aperture(cfg_dmi, apidx++,
				    NPX_CLN_L1_GRP_PERIPH_ADDR(i),
				    NPX_CLN_L1_GRP_PERIPH_SIZE,
				    msts_map[gr][i]);

	/* STU */
	for (i = 0; i < cn->num_grps; i++)
		npx_config_aperture(cfg_dmi, apidx++,
				    NPX_CLN_L1_GRP_STU_ADDR(i),
				    NPX_CLN_L1_GRP_STU_SIZE,
				    msts_map[gr][i]);

	/* CSM */
	il_bw = ilog2(cn->csm_banks_per_grp << cn->csm_bank_lsb);
	csm_size_mask = npx_size_mask(cn->csm_window) |
			((u64)(cn->num_grps - 1) << il_bw);
	for (i = 0; i < cn->num_grps; i++) {
		csm_base = NPX_CLN_CSM_ADDR | (i << il_bw);
		npx_config_aperture_direct(cfg_dmi, apidx++,
					   csm_base, csm_size_mask,
					   msts_map[gr][i], 12);
	}

	/* L2 core DCCM */
	npx_config_aperture(cfg_dmi, apidx++, NPX_CLN_L2_DCCM_ADDR,
			    cn->l2_dccm_size, msts_map[gr][gr]);

	/* Route all other top-matrix traffic to port 0 (bottom martix) */
	if (cn->revision == CLN_REVISION_V1R0)
		npx_config_aperture(cfg_dmi, 15, 0x0, 0x0, 0);
	else
		npx_config_aperture(cfg_dmi, apidx++, 0x0, 0x0, 0);

	/* Config L1 group bottom matrix */
	apidx = 0;
	cfg_dmi = cfg_ptr + NPX_CFG_L1_GRP_AXI_BOTTOM(gr);

	/* CSM banks */
	csm_bank_mask = npx_size_mask(cn->csm_window) |
			((u64)(cn->csm_banks_per_grp - 1) << cn->csm_bank_lsb);
	for (i = 0; i < cn->csm_banks_per_grp; i++) {
		csm_base = NPX_CLN_CSM_ADDR | (i << cn->csm_bank_lsb);
		npx_config_aperture_direct(cfg_dmi, apidx++,
					   csm_base, csm_bank_mask,
					   i, cn->csm_bank_lsb);
	}

	/* Local bottom apertures */
	port = cn->csm_banks_per_grp +
	       (cn->revision == CLN_REVISION_V1R0 ? 1 : 0);

	/* Slice peripheral */
	npx_config_aperture_lsb(cfg_dmi, apidx++,
				NPX_CLN_L1_GRP_PERIPH_ADDR(gr),
				NPX_CLN_L1_GRP_PERIPH_SIZE,
				port, cn->bottom_lsb);

	/* L2-DCCM */
	npx_config_aperture_lsb(cfg_dmi, apidx++, NPX_CLN_L2_DCCM_ADDR,
				cn->l2_dccm_size, port, cn->bottom_lsb);

	/* STU MMIO */
	npx_config_aperture_lsb(cfg_dmi, apidx++,
				NPX_CLN_L1_GRP_STU_ADDR(gr),
				NPX_CLN_L1_GRP_STU_SIZE,
				port, cn->bottom_lsb);

	/* Remaining traffic routes to the L1 NoC port */
	if (cn->revision == CLN_REVISION_V1R0)
		npx_config_aperture(cfg_dmi, apidx++, 0, 0,
				    cn->csm_banks_per_grp);

	/* Config ccm_demux */
	apidx = 0;
	port = 0;
	cfg_dmi = cfg_ptr + NPX_CFG_L1_GRP_CCM_DEMUX(gr);

	/* Access peripheral each SLICE */
	for (i = 0; i < cn->slice_per_grp; i++, port++)
		npx_config_aperture(cfg_dmi, apidx++,
				    NPX_CLN_L1_GRP_PERIPH_ADDR(gr) +
				    i * NPX_CLN_L1_SLICE_PERIPH_SIZE,
				    NPX_CLN_L1_SLICE_PERIPH_SIZE, port);

	/* STU */
	for (i = 0; i < cn->stu_per_grp; i++, port++)
		npx_config_aperture(cfg_dmi, apidx++,
				    NPX_CLN_L1_GRP_STU_ADDR(gr) +
				    i * NPX_CLN_L1_STU_SIZE,
				    NPX_CLN_L1_STU_SIZE, port);

	/* Accel L2-DCCM */
	npx_config_aperture(cfg_dmi, apidx++, NPX_CLN_L2_DCCM_ADDR,
			    cn->l2_dccm_size, port);
}

static int npx_csm_remap_aperture(void __iomem *ptr, int apidx, int virt_gr)
{
	int drop;

	switch (virt_gr) {
	case 1:
		drop = 0;
		break;
	case 2:
		drop = 1;
		break;
	case 4:
		drop = 2;
		break;
	case 8:
		drop = 3;
		break;
	}
	writel(drop, ptr + NPX_CFG_DECBASE + apidx * 4);

	return apidx + 2;
}

static int
npx_remap_aperture(void __iomem *ptr, int apidx,
		   const phys_addr_t apbase1, const u32 apsize1,
		   const phys_addr_t apbase2, const u32 apsize2, const int lsb)
{
	u32 base1 = apbase1 >> 12;
	u32 size1 = npx_dec_size(npx_size_mask(apsize1), 12);
	u32 base2 = apbase2 >> 12;
	u32 size2 = npx_dec_size(npx_size_mask(apsize2), 12);

	writel(base1, ptr + NPX_CFG_DECBASE + apidx * 4);
	writel(size1, ptr + NPX_CFG_DECSIZE + apidx * 4);

	size2 = size2 & ((1 << (lsb - 12)) - 1);
	writel(base2, ptr + NPX_CFG_DECBASE + (apidx + 1) * 4);
	writel(size2, ptr + NPX_CFG_DECSIZE + (apidx + 1) * 4);

	return apidx + 2;
}

static void
npx_config_remap(void __iomem *cfg_ptr, struct snps_npu_cn *cn, int gr)
{
	void __iomem *cfg_dmi;
	phys_addr_t saddr;
	phys_addr_t caddr;
	int apidx = 0;
	int lsb;
	int i;

	cfg_dmi = cfg_ptr + NPX_CFG_L1_CSM_REMAP(gr);
	apidx = npx_csm_remap_aperture(cfg_dmi, apidx, cn->num_grps);

	/* Config sfty ccm remap */
	if (cn->safety_lvl > 0) {
		lsb = ilog2(NPX_CLN_L1_GRP_SFTY_SIZE);
		for (i = 0; i < cn->slice_per_grp; i++) {
			/* Remap host view of slice safety MMIO */
			caddr = cn->cfg_phys + gr * NPX_CFG_L1_GRP_OFFSET +
				0x4000 + i * NPX_CLN_L1_GRP_SFTY_SIZE;
			saddr = NPX_CLN_L1_GRP_SFTY(gr, i);
			apidx = npx_remap_aperture(cfg_dmi, apidx,
						   caddr, NPX_CLN_L1_GRP_SFTY_SIZE,
						   saddr, NPX_CLN_L1_GRP_SFTY_SIZE, lsb);
			/* L2 safety MMIO remap */
			caddr = NPX_CBU_L2_CFG_AXI_ADDR + gr * NPX_CFG_L1_GRP_OFFSET +
				0x4000 + i * NPX_CLN_L1_GRP_SFTY_SIZE;
			apidx = npx_remap_aperture(cfg_dmi, apidx,
						   caddr, NPX_CLN_L1_GRP_SFTY_SIZE,
						   saddr, NPX_CLN_L1_GRP_SFTY_SIZE, lsb);
		}
	}
}

static int npx_powerup_core(struct snps_accel_rproc *aproc, u32 clid, u32 cid)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	int count = 10;

	if (fn->get_status(ctrl, clid, cid) & ARCSYNC_CORE_POWERDOWN) {
		fn->clk_ctrl(ctrl, clid, cid, ARCSYNC_CLK_DIS);
		fn->power_ctrl(ctrl, clid, cid, ARCSYNC_POWER_UP);
		fn->clk_ctrl(ctrl, clid, cid, ARCSYNC_CLK_EN);
		while ((fn->get_status(ctrl, clid, cid) & ARCSYNC_CORE_POWERDOWN) && --count)
			udelay(1);
	}

	return count ? 0 : -EBUSY;
}

static int npx_powerdown_core(struct snps_accel_rproc *aproc, u32 clid, u32 cid)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	int count = 10;

	if (!(fn->get_status(ctrl, clid, cid) & ARCSYNC_CORE_POWERDOWN)) {
		fn->halt(ctrl, clid, cid);
		while (!(fn->get_status(ctrl, clid, cid) & ARCSYNC_CORE_HALTED) && --count)
			udelay(1);
		fn->clk_ctrl(ctrl, clid, cid, ARCSYNC_CLK_DIS);
		fn->power_ctrl(ctrl, clid, cid, ARCSYNC_POWER_DOWN);
		count = 10;
		while (!(fn->get_status(ctrl, clid, cid) & ARCSYNC_CORE_POWERDOWN) && --count)
			udelay(1);
	}

	return count ? 0 : -EBUSY;
}

static int npx_reset_cluster_grps(struct snps_accel_rproc *aproc, u32 rst_code)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 clid = aproc->cluster_id;
	int grp;
	int i;

	if (aproc->ctrl.ver >= 2) {
		fn->reset_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP,
					rst_code);
		/* reset L2C cores inside the L2 group */
		fn->reset(ctrl, clid, NPX_COREID_L2C0, rst_code);
		if (aproc->cn.num_slices >= 8)
			fn->reset(ctrl, clid, aproc->cn.num_slices + 1,
				  rst_code);

		for (grp = 0; grp < aproc->cn.num_grps; grp++) {
			fn->reset_cluster_group(ctrl, clid, ARCSYNC_NPX_L1GRP0 + grp,
						rst_code);
			/* reset cores inside the group */
			for (i = 0; i < aproc->cn.slice_per_grp; i++)
				fn->reset(ctrl, clid,
					  NPX_COREID_L1C0 + grp * aproc->cn.slice_per_grp + i,
					  rst_code);
		}
	}

	return 0;
}

static int npx_powerup_cluster_grps(struct snps_accel_rproc *aproc)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 clid = aproc->cluster_id;
	int slice_offset;
	int grp;
	int i;

	if (aproc->ctrl.ver >= 2) {
		fn->clk_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ARCSYNC_CLK_DIS);
		fn->power_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ARCSYNC_POWER_UP);
		fn->clk_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ARCSYNC_CLK_EN);
		npx_powerup_core(aproc, clid, NPX_COREID_L2C0);
		if (aproc->cn.num_slices >= 8)
			npx_powerup_core(aproc, clid, aproc->cn.num_slices + 1);
		for (grp = 0; grp < aproc->cn.num_grps; grp++) {
			fn->clk_ctrl_cluster_group(ctrl, clid,
						   ARCSYNC_NPX_L1GRP0 + grp,
						   ARCSYNC_CLK_DIS);
			fn->power_ctrl_cluster_group(ctrl, clid,
						     ARCSYNC_NPX_L1GRP0 + grp,
						     ARCSYNC_POWER_UP);
			fn->clk_ctrl_cluster_group(ctrl, clid,
						   ARCSYNC_NPX_L1GRP0 + grp,
						   ARCSYNC_CLK_EN);
			slice_offset = grp * aproc->cn.slice_per_grp;
			for (i = 0; i < aproc->cn.slice_per_grp; i++)
				npx_powerup_core(aproc, clid,
						 NPX_COREID_L1C0 + slice_offset + i);
		}
	}

	return 0;
}

static int npx_powerdown_cluster_grps(struct snps_accel_rproc *aproc)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 clid = aproc->cluster_id;
	int slice_offset;
	int grp;
	int i;

	if (aproc->ctrl.ver >= 2) {
		for (grp = 0; grp < aproc->cn.num_grps; grp++) {
			fn->clk_ctrl_cluster_group(ctrl, clid,
						   ARCSYNC_NPX_L1GRP0 + grp,
						   ARCSYNC_CLK_DIS);
			fn->power_ctrl_cluster_group(ctrl, clid,
						     ARCSYNC_NPX_L1GRP0 + grp,
						     ARCSYNC_POWER_DOWN);
			slice_offset = grp * aproc->cn.slice_per_grp;
			for (i = 0; i < aproc->cn.slice_per_grp; i++)
				npx_powerdown_core(aproc, clid,
						 NPX_COREID_L1C0 + slice_offset + i);
		}
		fn->clk_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ARCSYNC_CLK_DIS);
		fn->power_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ARCSYNC_POWER_DOWN);
		npx_powerdown_core(aproc, clid, NPX_COREID_L2C0);
		if (aproc->cn.num_slices >= 8)
			npx_powerdown_core(aproc, clid, aproc->cn.num_slices + 1);
	}

	return 0;
}

static int npx_clk_ctrl_cluster_grps(struct snps_accel_rproc *aproc, u32 ctrl_val)
{
	struct device *ctrl = aproc->ctrl.dev;
	const struct snps_accel_rproc_ctrl_fn *fn = &aproc->ctrl.fn;
	u32 clid = aproc->cluster_id;
	int grp;
	int i;

	if (aproc->ctrl.ver < 2)
		return 0;

	fn->clk_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L2GRP, ctrl_val);
	fn->clk_ctrl(ctrl, clid, NPX_COREID_L2C0, ctrl_val);
	if (aproc->cn.num_slices >= 8)
		fn->clk_ctrl(ctrl, clid, aproc->cn.num_slices + 1, ctrl_val);
	for (grp = 0; grp < aproc->cn.num_grps; grp++) {
		fn->clk_ctrl_cluster_group(ctrl, clid, ARCSYNC_NPX_L1GRP0 + grp, ctrl_val);
		for (i = 0; i < aproc->cn.slice_per_grp; i++)
			fn->clk_ctrl(ctrl, clid,
				     NPX_COREID_L1C0 + grp * aproc->cn.slice_per_grp + i,
				     ctrl_val);
	}

	return 0;
}

int npx_stop_cluster_default(struct snps_accel_rproc *npu)
{
	npx_reset_cluster_grps(npu, ARCSYNC_RESET_ASSERT);
	if (npu->ctrl.has_pmu)
		npx_powerdown_cluster_grps(npu);
	else
		npx_clk_ctrl_cluster_grps(npu, ARCSYNC_CLK_DIS);

	return 0;
}

int npx_setup_cluster_default(struct snps_accel_rproc *npu)
{
	void __iomem *cfg_ptr;
	struct device_node *of_node = npu->device->of_node;
	struct device_node *npu_cfg_np;
	struct resource cfg_mem;
	int ret;
	int i;

	npu_cfg_np = of_parse_phandle(of_node, "snps,npu-cfg", 0);
	if (!npu_cfg_np) {
		dev_dbg(npu->device, "Skip NPX cluster setup\n");
		return 0;
	}

	/* Get NPU CFG area base address */
	ret = of_address_to_resource(npu_cfg_np, 0, &cfg_mem);
	if (ret < 0) {
		dev_err(npu->device, "NPU cfg mem aperture not found\n");
		return ret;
	}

	cfg_ptr = ioremap(cfg_mem.start, resource_size(&cfg_mem));
	if (!cfg_ptr)
		return -EFAULT;

	dev_dbg(npu->device, "NPU CFG base %pap\n", &cfg_mem.start);

	npu->cn.num_slices = NPU_DEF_NUM_SLICES;
	npu->cn.csm_banks_per_grp = NPU_DEF_CSM_BANKS_PER_GRP;
	npu->cn.stu_per_grp = NPU_DEF_NUM_STU_PER_GRP;
	npu->cn.safety_lvl = NPU_DEF_SAFETY_LEVEL;
	npu->cn.csm_size = NPU_DEF_CSM_SIZE;
	npu->cn.map_start = NPU_DEF_CLN_MAP_START;
	npu->cn.skip_setup = 0;
	npu->cn.revision = NPU_DEF_CLN_REVISION;
	npu->cn.l2_dccm_size = NPU_DEF_L2_DCCM_SIZE;
	npu->cn.csm_bank_lsb = NPU_DEF_CSM_BANK_LSB;
	npu->cn.csm_window = NPU_DEF_CSM_WINDOW;

	/* Get groups properties and update defaults */
	of_property_read_u32(npu_cfg_np, "snps,npu-slice-num",
			     &npu->cn.num_slices);
	of_property_read_u32(npu_cfg_np, "snps,csm-banks-per-group",
			     &npu->cn.csm_banks_per_grp);
	of_property_read_u32(npu_cfg_np, "snps,stu-per-group",
			     &npu->cn.stu_per_grp);
	of_property_read_u32(npu_cfg_np, "snps,cln-safety-lvl",
			     &npu->cn.safety_lvl);
	of_property_read_u32(npu_cfg_np, "snps,csm-size",
			     &npu->cn.csm_size);
	of_property_read_u32(npu_cfg_np, "snps,cln-map-start",
			     &npu->cn.map_start);
	of_property_read_u32(npu_cfg_np, "snps,cln-revision",
			     &npu->cn.revision);

	ret = of_property_read_u32(npu_cfg_np, "snps,csm-bank-lsb",
				   &npu->cn.csm_bank_lsb);
	if (ret && npu->cn.revision == CLN_REVISION_V1R5)
		npu->cn.csm_bank_lsb = 10;
	npu->cn.bottom_lsb = npu->cn.csm_bank_lsb;

	of_property_read_u32(npu_cfg_np, "snps,csm-window",
			     &npu->cn.csm_window);

	ret = of_property_read_u32(npu_cfg_np, "snps,npu-group-num",
			     &npu->cn.num_grps);
	if (ret) {
		if (npu->cn.num_slices <= 4)
			npu->cn.num_grps = 1;
		else if (npu->cn.num_slices <= 8)
			npu->cn.num_grps = 2;
		else
			npu->cn.num_grps = 4;
	}
	npu->cn.slice_per_grp = npu->cn.num_slices / npu->cn.num_grps;
	npu->cn.cfg_phys = cfg_mem.start;
	npu->cn.skip_setup = of_property_read_bool(npu_cfg_np, "snps,skip-cln-setup");

	dev_dbg(npu->device, "NPU slice num: %d\n", npu->cn.num_slices);
	dev_dbg(npu->device, "Num grps: %d\n", npu->cn.num_grps);
	dev_dbg(npu->device, "Slices per grp: %d\n", npu->cn.slice_per_grp);
	dev_dbg(npu->device, "Num csm banks per grp: %d\n", npu->cn.csm_banks_per_grp);
	dev_dbg(npu->device, "Slices per grp: %d\n", npu->cn.slice_per_grp);
	dev_dbg(npu->device, "STU per grp: %d\n", npu->cn.stu_per_grp);
	dev_dbg(npu->device, "CSM size: 0x%x\n", npu->cn.csm_size);
	dev_dbg(npu->device, "CSM window: 0x%x\n", npu->cn.csm_window);
	dev_dbg(npu->device, "CSM bank LSB: %d\n", npu->cn.csm_bank_lsb);
	dev_dbg(npu->device, "CLN revision: %d\n", npu->cn.revision);
	dev_dbg(npu->device, "CLN map start: 0x%x\n", npu->cn.map_start);

	/* Reset NPX cluster groups */
	npx_reset_cluster_grps(npu, ARCSYNC_RESET_DEASSERT);
	if (npu->ctrl.has_pmu)
		npx_powerup_cluster_grps(npu);
	else
		npx_clk_ctrl_cluster_grps(npu, ARCSYNC_CLK_EN);

	/* Setup Cluster Network */
	if (!npu->cn.skip_setup) {
		npx_config_l2_grp(cfg_ptr, &npu->cn);
		for (i = 0; i < npu->cn.num_grps; i++) {
			dev_dbg(npu->device, "Config L1 group %d\n", i);
			npx_config_cln_grp(cfg_ptr, &npu->cn, i);
			npx_config_remap(cfg_ptr, &npu->cn, i);
		}
	}

	iounmap(cfg_ptr);
	return 0;
}
