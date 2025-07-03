// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <uapi/misc/snps_accel.h>
#include "snps_accel_drv.h"

static struct snps_accel_mem_buffer *
snps_accel_mbuf_alloc(struct snps_accel_mem_ctx *mem, size_t size, 
		      enum dma_data_direction dma_dir)
{
	struct page *page;
	struct snps_accel_mem_buffer *mbuf = NULL;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);
	struct device *dmabuf_dev = mem->dev->parent;

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf)
		return NULL;

	/* Allocate buffer in direct memory */
	page = dma_alloc_pages(dmabuf_dev, PAGE_ALIGN(size), &mbuf->da,
				dma_dir, GFP_KERNEL | __GFP_NOWARN);
	if (!page) {
		dev_err(mem->dev, "Failed to allocate contiguous memory for buffer\n");
		return NULL;
	}
	mbuf->alloc_da = mbuf->da;
	mbuf->ctx = mem;
	mbuf->dev = dmabuf_dev;
	mbuf->va = page_address(page);
	mbuf->pa =  page_to_pfn(page) << PAGE_SHIFT;
	mbuf->size = PAGE_ALIGN(size);
	mbuf->dma_dir = dma_dir;

	mutex_init(&mbuf->lock);
	INIT_LIST_HEAD(&mbuf->attachments);

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_file_priv_get(fpriv);
	return mbuf;
}

static void
snps_accel_mbuf_free(struct snps_accel_mem_ctx *mem, 
		     struct snps_accel_mem_buffer *mbuf,
		     enum dma_data_direction dma_dir)
{
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mutex_lock(&mem->list_lock);
	list_del(&mbuf->ctx_link);
	mutex_unlock(&mem->list_lock);

	dma_free_pages(mbuf->dev, mbuf->size,
					virt_to_page(mbuf->va),
					mbuf->alloc_da, dma_dir);

	kfree(mbuf);
	snps_accel_file_priv_put(fpriv);
}

static struct snps_accel_mem_buffer *
snps_accel_dmabuf_find_by_fd(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_mem_buffer *mbuf = NULL;

	mutex_lock(&mem->list_lock);
	list_for_each_entry(mbuf, &mem->mlist, ctx_link) {
		if (mbuf->fd == fd) {
			mutex_unlock(&mem->list_lock);
			return mbuf;
		}
	}
	mutex_unlock(&mem->list_lock);

	return NULL;
}

static bool snps_accel_dmabuf_is_contig(struct sg_table *sgt)
{
	struct scatterlist *s;
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned int i;

	for_each_sgtable_dma_sg(sgt, s, i) {
		if (sg_dma_address(s) != expected)
			return 0;
		expected += sg_dma_len(s);
	}
	return 1;
}

static int snps_accel_dmabuf_attach_device(struct dma_buf *dmabuf,
					   struct device *dev,
					   struct snps_accel_mem_buffer *mbuf,
					   enum dma_data_direction dma_dir)
{
	struct dma_buf_attachment *dba;
	struct sg_table *sgt;

	dba = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(dba)) 
		return PTR_ERR(dba);

	sgt = dma_buf_map_attachment(dba, dma_dir);
	if (IS_ERR(sgt)) {
		dma_buf_detach(dmabuf, dba);
		return PTR_ERR(sgt);
	}

	mbuf->da = sg_dma_address(sgt->sgl);
	mbuf->dmasgt = sgt;
	mbuf->import_attach = dba;

	return 0;
}

static void 
snps_accel_dmabuf_detach_device(struct snps_accel_mem_buffer *mbuf)
{
	if (mbuf->dmasgt)
		dma_buf_unmap_attachment(mbuf->import_attach, mbuf->dmasgt,
					 mbuf->dma_dir);
	dma_buf_detach(mbuf->dmabuf, mbuf->import_attach);
	dma_buf_put(mbuf->import_attach->dmabuf);
}

static void snps_accel_dmabuf_op_release(struct dma_buf *dmabuf)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_mem_ctx *mem = mbuf->ctx;

	snps_accel_dmabuf_detach_device(mbuf);
	snps_accel_mbuf_free(mem, mbuf, mbuf->dma_dir);
}

static int
snps_accel_dmabuf_op_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	int ret = 0;

	if (PAGE_ALIGN(size) != mbuf->size)
		return -EINVAL;

	ret = dma_mmap_pages(mbuf->dev, vma, mbuf->size, virt_to_page(mbuf->va));
	if (ret)
		return ret;

	return 0;
}

static int snps_accel_dmabuf_op_attach(struct dma_buf *dmabuf,
				       struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mbuf->ctx);
	int ret;

	dba = kzalloc(sizeof(*dba), GFP_KERNEL);
	if (!dba)
		return -ENOMEM;

	ret = dma_get_sgtable(mbuf->dev, &dba->sgt, mbuf->va,
			      mbuf->pa, mbuf->size);
	if (ret < 0) {
		dev_err(mbuf->dev, "Failed to get scatter list from DMA API\n");
		kfree(dba);
		return -EINVAL;
	}

	dba->dev = attachment->dev;
	INIT_LIST_HEAD(&dba->node);
	attachment->priv = dba;
	dba->mapped = false;

	mutex_lock(&mbuf->lock);
	list_add(&dba->node, &mbuf->attachments);
	mutex_unlock(&mbuf->lock);
	snps_accel_file_priv_get(fpriv);

	return 0;
}

static void snps_accel_dmabuf_op_detach(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mbuf->ctx);

	mutex_lock(&mbuf->lock);
	list_del(&dba->node);
	mutex_unlock(&mbuf->lock);
	sg_free_table(&dba->sgt);
	kfree(dba);
	snps_accel_file_priv_put(fpriv);
}

static struct sg_table *
snps_accel_dmabuf_op_map(struct dma_buf_attachment *attachment,
			 enum dma_data_direction dir)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct sg_table *table;
	int ret;

	table = &dba->sgt;
	dba->mapped = true;

	ret = dma_map_sgtable(attachment->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);

	return table;
}

static void snps_accel_dmabuf_op_unmap(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	struct snps_accel_dmabuf_attachment *dba = attach->priv;

	dba->mapped = false;
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static int snps_accel_dmabuf_op_begin_cpu_access(struct dma_buf *dmabuf,
						 enum dma_data_direction direction)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_dmabuf_attachment *dba;

	mutex_lock(&mbuf->lock);

	list_for_each_entry(dba, &mbuf->attachments, node) {
		if (!dba->mapped)
			continue;
		dma_sync_sgtable_for_cpu(dba->dev, &dba->sgt, direction);
	}
	mutex_unlock(&mbuf->lock);

	return 0;
}

static int snps_accel_dmabuf_op_end_cpu_access(struct dma_buf *dmabuf,
					       enum dma_data_direction direction)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_dmabuf_attachment *dba;

	mutex_lock(&mbuf->lock);

	list_for_each_entry(dba, &mbuf->attachments, node) {
		if (!dba->mapped)
			continue;
		dma_sync_sgtable_for_device(dba->dev, &dba->sgt, direction);
	}
	mutex_unlock(&mbuf->lock);

	return 0;
}

static const struct dma_buf_ops snps_accel_dmabuf_ops = {
	.attach = snps_accel_dmabuf_op_attach,
	.detach = snps_accel_dmabuf_op_detach,
	.map_dma_buf = snps_accel_dmabuf_op_map,
	.unmap_dma_buf = snps_accel_dmabuf_op_unmap,
	.begin_cpu_access = snps_accel_dmabuf_op_begin_cpu_access,
	.end_cpu_access = snps_accel_dmabuf_op_end_cpu_access,
	.mmap = snps_accel_dmabuf_op_mmap,
	.release = snps_accel_dmabuf_op_release,
};

void snps_accel_app_mem_init(struct device *dev, struct snps_accel_mem_ctx *mem)
{
	mem->dev = dev;
	mutex_init(&mem->list_lock);
	INIT_LIST_HEAD(&mem->mlist);
}

void snps_accel_app_release_import(struct snps_accel_mem_ctx *mem)
{
	struct snps_accel_mem_buffer *mbuf, *nmb;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mutex_lock(&mem->list_lock);
	list_for_each_entry_safe(mbuf, nmb, &mem->mlist, ctx_link) {
		if (mbuf->ctx == NULL && mbuf->import_attach) {
			snps_accel_dmabuf_detach_device(mbuf);
			list_del(&mbuf->ctx_link);
			kfree(mbuf);
			snps_accel_file_priv_put(fpriv);
		}
	}
	mutex_unlock(&mem->list_lock);
}

static inline enum dma_data_direction snps_accel_app_dma_direction(u32 dflags)
{
	unsigned ret = DMA_BIDIRECTIONAL;

	if (dflags == SNPS_ACCEL_IO_R)
		ret = DMA_FROM_DEVICE;
	if (dflags == SNPS_ACCEL_IO_W)
		ret = DMA_TO_DEVICE;

	return ret;
}

struct snps_accel_mem_buffer *snps_accel_app_dmabuf_create(struct snps_accel_mem_ctx *mem,
							   u64 size, u32 dflags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct snps_accel_mem_buffer *mbuf = NULL;
	int fd;
	enum dma_data_direction dma_dir = snps_accel_app_dma_direction(dflags);

	mbuf = snps_accel_mbuf_alloc(mem, size, dma_dir);
	if (mbuf == NULL)
		return NULL;

	exp_info.ops = &snps_accel_dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = mbuf;
	mbuf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(mbuf->dmabuf)) {
		dev_dbg(mem->dev, "Failed to create dmabuf\n");
		snps_accel_mbuf_free(mem, mbuf, dma_dir);
		return NULL;
	}

	fd = dma_buf_fd(mbuf->dmabuf, O_ACCMODE | O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(mbuf->dmabuf);
		return NULL;
	}
	mbuf->fd = fd;

	if (snps_accel_dmabuf_attach_device(mbuf->dmabuf, mbuf->dev,
					    mbuf, mbuf->dma_dir) != 0) {
		dev_err(mem->dev, "Failed to attach dmabuf to device\n");
		dma_buf_put(mbuf->dmabuf);
		return NULL;
	}

	return mbuf;
}

int snps_accel_app_dmabuf_info(struct snps_accel_dmabuf_info *info)
{
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;

	dmabuf = dma_buf_get(info->fd);
	if (!dmabuf)
		return -EINVAL;

	mbuf = (struct snps_accel_mem_buffer *)dmabuf->priv;
	info->addr = mbuf->da;
	info->size = mbuf->size;

	dev_dbg(mbuf->dev, "dmabuf info: va/pa/da %px/%pa/%pad, size %zu\n",
			mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	dma_buf_put(dmabuf);
	return 0;
}

void snps_accel_app_dmabuf_release(struct snps_accel_mem_buffer *mbuf)
{
	dma_buf_put(mbuf->dmabuf);
}

int snps_accel_app_dmabuf_import(struct snps_accel_mem_ctx *mem, int fd)
{
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;
	int ret;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);
	struct device *dmabuf_dev = mem->dev->parent;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(mem->dev, "Failed to get dma_buf with fd %d\n", fd);
		return -EINVAL;
	}

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf) {
		dma_buf_put(dmabuf);
		ret = -ENOMEM;
		goto err_alloc;
	}

	mbuf->dma_dir = DMA_BIDIRECTIONAL;
	mbuf->dev = dmabuf_dev;
	ret = snps_accel_dmabuf_attach_device(dmabuf, mem->dev,
					      mbuf, mbuf->dma_dir);
	if (ret != 0) {
		dev_err(mem->dev, "Failed to attach dmabuf to device\n");
		goto err_attach;
	}

	if (!snps_accel_dmabuf_is_contig(mbuf->dmasgt)) {
		ret = -EINVAL;
		goto err_notcontig;
	}

	mbuf->fd = fd;
	mbuf->dmabuf = dmabuf;
	mbuf->size = dmabuf->size;
	mbuf->va = NULL;

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_file_priv_get(fpriv);

	return 0;

err_notcontig:
	dma_buf_unmap_attachment(mbuf->import_attach, mbuf->dmasgt,
				 mbuf->dma_dir);
	dma_buf_detach(dmabuf, mbuf->import_attach);
err_attach:
	kfree(mbuf);
err_alloc:
	dma_buf_put(dmabuf);
	return ret;
}

int snps_accel_app_dmabuf_detach(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_mem_buffer *mbuf;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mbuf = snps_accel_dmabuf_find_by_fd(mem, fd);
	if (!mbuf) {
		dev_err(mem->dev, "Failed to find imported dmabuf with fd %d\n", fd);
		return -EINVAL;
	}

	/* This check allows to call detach safely for non-imported buffers */
	if (mbuf->ctx == NULL) {
		snps_accel_dmabuf_detach_device(mbuf);

		mutex_lock(&mem->list_lock);
		list_del(&mbuf->ctx_link);
		mutex_unlock(&mem->list_lock);

		kfree(mbuf);
		snps_accel_file_priv_put(fpriv);
	}

	return 0;
}
