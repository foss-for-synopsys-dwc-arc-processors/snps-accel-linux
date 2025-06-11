// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Synopsys, Inc. (www.synopsys.com)
 */

#include <uapi/misc/snps_accel.h>

#include "snps_accel_drv.h"

/**
 * snps_accel_mbuf_alloc - Allocate a memory buffer.
 * @mem: Memory context.
 * @size: Size.
 * @direction: DMA direction.
 *
 * Return: Address of a memory buffer on success, otherwise NULL.
 */
static struct snps_accel_mem_buffer *
snps_accel_mbuf_alloc(struct snps_accel_mem_ctx *mem, size_t size,
		      enum dma_data_direction direction)
{
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mem);
	struct device *dmabuf_dev = mem->dev;
	struct snps_accel_mem_buffer *mbuf;
	struct page *page;

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf)
		return NULL;

	/* Allocate buffer in direct memory */
	page = dma_alloc_pages(dmabuf_dev, PAGE_ALIGN(size), &mbuf->da,
			       direction, GFP_KERNEL | __GFP_NOWARN);
	if (!page) {
		dev_err(dmabuf_dev,
			"Failed to allocate contiguous DMA pages\n");
		return NULL;
	}

	mbuf->ctx = mem;
	mbuf->dev = dmabuf_dev;
	mbuf->va = page_address(page);
	mbuf->pa = page_to_pfn(page) << PAGE_SHIFT;
	mbuf->size = PAGE_ALIGN(size);
	mbuf->dma_dir = direction;

	mutex_init(&mbuf->lock);
	INIT_LIST_HEAD(&mbuf->attachments);

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_ctrl_priv_get(priv);
	return mbuf;
}

/**
 * snps_accel_mbuf_free - Free a memory buffer.
 * @mem: Memory context.
 * @mbuf: Memory buffer.
 * @direction: DMA direction.
 */
static void snps_accel_mbuf_free(struct snps_accel_mem_ctx *mem,
				 struct snps_accel_mem_buffer *mbuf,
				 enum dma_data_direction direction)
{
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mem);

	mutex_lock(&mem->list_lock);
	list_del(&mbuf->ctx_link);
	mutex_unlock(&mem->list_lock);

	dma_free_pages(mbuf->dev, mbuf->size, virt_to_page(mbuf->va),
		       mbuf->da, direction);

	kfree(mbuf);
	snps_accel_ctrl_priv_put(priv);
}

/**
 * snps_accel_dmabuf_find_by_fd - Find a memory buffer by file descriptor.
 * @mem: Memory context.
 * @fd: File descriptor.
 *
 * Return: Address of a memory buffer on success, otherwise NULL.
 */
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

/**
 * snps_accel_dmabuf_is_contiguous - Check if DMA buffer is contiguous.
 * @sgt: Scatter-gather table.
 *
 * Contiguous emans the state of data or memory being stored in a single,
 * uninterrupted block.
 *
 * Return: %true if DMA buffer is contiguous, otherwise %false.
 */
static bool snps_accel_dmabuf_is_contiguous(struct sg_table *sgt)
{
	struct scatterlist *s;
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned int i;

	for_each_sgtable_dma_sg(sgt, s, i) {
		if (sg_dma_address(s) != expected)
			return false;

		expected += sg_dma_len(s);
	}

	return true;
}

/**
 * snps_accel_dmabuf_attach_device - Attach a memory buffer to a device.
 * @dmabuf: DMA buffer.
 * @dev: Device.
 * @mbuf: Memory buffer.
 * @direction: DMA direction.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_dmabuf_attach_device(struct dma_buf *dmabuf, struct device *dev,
				struct snps_accel_mem_buffer *mbuf,
				enum dma_data_direction direction)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;

	attachment = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attachment)) {
		dev_err(dev, "Failed to get attachment of DMA buffer: %ld\n",
			PTR_ERR(attachment));
		return PTR_ERR(attachment);
	}

	sgt = dma_buf_map_attachment(attachment, direction);
	if (IS_ERR(sgt)) {
		dma_buf_detach(dmabuf, attachment);
		dev_err(dev, "Failed to map attachment of DMA buffer: %ld\n",
			PTR_ERR(sgt));
		return PTR_ERR(sgt);
	}

	/*
	 * Some DMA buffers might not be allocated by a device instance of
	 * Synopsys AI accelerator system, but they are importing to the
	 * Synopsys AI accelerator driver. In order to allow us to be able to
	 * free those DMA buffers correctly on calling dma_free_pages().
	 * We need to identify those DMA buffers and use sg_dma_address() to
	 * get mapped DMA handles specific to us, and then the DMA handle will
	 * be used to free those DMA buffers.
	 * By contrast, for DMA buffers allocated by us, mapped DMA handles
	 * returned by dma_alloc_pages() can be used for dma_free_pages()
	 * directly.
	 */
	if (mbuf->ctx == NULL)
		mbuf->da = sg_dma_address(sgt->sgl);

	mbuf->dmasgt = sgt;
	mbuf->import_attach = attachment;

	return 0;
}

/**
 * snps_accel_dmabuf_detach_device - Detach a memory buffer.
 * @mbuf: Memory buffer.
 */
static void
snps_accel_dmabuf_detach_device(struct snps_accel_mem_buffer *mbuf)
{
	if (mbuf->dmasgt)
		dma_buf_unmap_attachment(mbuf->import_attach,
					 mbuf->dmasgt, mbuf->dma_dir);
	dma_buf_detach(mbuf->dmabuf, mbuf->import_attach);
	dma_buf_put(mbuf->dmabuf);
}

/**
 * snps_accel_dmabuf_op_release - Release operation of DMA buffer.
 * @dmabuf: DMA buffer.
 */
static void snps_accel_dmabuf_op_release(struct dma_buf *dmabuf)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_mem_ctx *mem = mbuf->ctx;

	dev_dbg(mbuf->dev,
		"dmabuf op release: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	snps_accel_dmabuf_detach_device(mbuf);
	snps_accel_mbuf_free(mem, mbuf, mbuf->dma_dir);
}

/**
 * snps_accel_dmabuf_op_mmap - Memory map operation of DMA buffer.
 * @dmabuf: DMA buffer.
 * @vma: Virtual memory area.
 */
static int
snps_accel_dmabuf_op_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	int ret = 0;

	if (PAGE_ALIGN(size) != mbuf->size) {
		dev_err(mbuf->dev,
			"Invalid size of mmap (%xz vs %xz)\n", PAGE_ALIGN(size),
			mbuf->size);
		return -EINVAL;
	}

	ret = dma_mmap_pages(mbuf->dev, vma, mbuf->size,
			     virt_to_page(mbuf->va));
	if (ret) {
		dev_err(mbuf->dev, "Failed to mmap DMA pages: %d\n", ret);
		return ret;
	}

	dev_dbg(mbuf->dev,
		"dmabuf op mmap: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	return 0;
}

/**
 * snps_accel_dmabuf_op_attach - Attach operation of DMA buffer.
 * @dmabuf: DMA buffer.
 * @attachment: Attachment of DMA buffer.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int snps_accel_dmabuf_op_attach(struct dma_buf *dmabuf,
				       struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_ctrl_priv *priv =
			to_snps_accel_ctrl_priv(mbuf->ctx);
	int ret;

	dba = kzalloc(sizeof(*dba), GFP_KERNEL);
	if (!dba)
		return -ENOMEM;

	ret = dma_get_sgtable(mbuf->dev, &dba->sgt, mbuf->va,
			      mbuf->pa, mbuf->size);
	if (ret < 0) {
		dev_err(mbuf->dev,
			"Failed to get DMA scatter-gather table: %d\n", ret);
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

	snps_accel_ctrl_priv_get(priv);

	dev_dbg(mbuf->dev,
		"dmabuf op attach: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	return 0;
}

/**
 * snps_accel_dmabuf_op_detach - Detach operation of DMA buffer.
 * @dmabuf: DMA buffer.
 * @attachment: Attachment of DMA buffer.
 */
static void snps_accel_dmabuf_op_detach(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mbuf->ctx);

	dev_dbg(mbuf->dev,
		"dmabuf op detach: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	mutex_lock(&mbuf->lock);
	list_del(&dba->node);
	mutex_unlock(&mbuf->lock);

	sg_free_table(&dba->sgt);
	kfree(dba);

	snps_accel_ctrl_priv_put(priv);
}

/**
 * snps_accel_dmabuf_op_map - Map operation of DMA buffer.
 * @attachment: Attachment of DMA buffer.
 * @direction: DMA direction.
 *
 * Return: Address of a scatter-gather table on success, otherwise an appropriate error code.
 */
static struct sg_table *
snps_accel_dmabuf_op_map(struct dma_buf_attachment *attachment,
			 enum dma_data_direction direction)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct snps_accel_mem_buffer *mbuf = attachment->dmabuf->priv;
	struct sg_table *table;
	int ret;

	table = &dba->sgt;
	dba->mapped = true;

	ret = dma_map_sgtable(attachment->dev, table, direction, 0);
	if (ret) {
		dev_err(attachment->dev,
			"Failed to map scatter-gather table: %d\n", ret);
		table = ERR_PTR(ret);
	}

	dev_dbg(attachment->dev,
		"dmabuf op map: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	return table;
}

/**
 * snps_accel_dmabuf_op_unmap - Unmap operation of DMA buffer.
 * @attachment: Attachment of DMA buffer.
 * @table: A scatter-gather table.
 * @direction: DMA direction.
 */
static void snps_accel_dmabuf_op_unmap(struct dma_buf_attachment *attachment,
				       struct sg_table *table,
				       enum dma_data_direction direction)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct snps_accel_mem_buffer *mbuf = attachment->dmabuf->priv;

	dba->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, 0);

	dev_dbg(attachment->dev,
		"dmabuf op unmap: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);
}

/**
 * snps_accel_dmabuf_op_begin_cpu_access - Begin CPU access operation of DMA buffer.
 * @dmabuf: DMA buffer.
 * @direction: DMA direction.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_dmabuf_op_begin_cpu_access(struct dma_buf *dmabuf,
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

/**
 * snps_accel_dmabuf_op_end_cpu_access - End CPU access operation of DMA buffer.
 * @dmabuf: DMA buffer.
 * @direction: DMA direction.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
static int
snps_accel_dmabuf_op_end_cpu_access(struct dma_buf *dmabuf,
				    enum dma_data_direction direction)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_dmabuf_attachment *dba;

	mutex_lock(&mbuf->lock);
	list_for_each_entry(dba, &mbuf->attachments, node) {
		if (!dba->mapped)
			continue;

		dma_sync_sgtable_for_device(dba->dev, &dba->sgt,
					    direction);
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

/**
 * snps_accel_mem_init - Initialize memory context.
 * @dev: Device.
 * @mem: Memory context.
 */
void snps_accel_mem_init(struct device *dev, struct snps_accel_mem_ctx *mem)
{
	mem->dev = dev;
	mutex_init(&mem->list_lock);
	INIT_LIST_HEAD(&mem->mlist);
}

/**
 * snps_accel_release_import - Release import.
 * @mem: Memory context.
 */
void snps_accel_release_import(struct snps_accel_mem_ctx *mem)
{
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mem);
	struct snps_accel_mem_buffer *mbuf, *next;

	mutex_lock(&mem->list_lock);
	list_for_each_entry_safe(mbuf, next, &mem->mlist, ctx_link) {
		if (mbuf->ctx == NULL && mbuf->import_attach) {
			snps_accel_dmabuf_detach_device(mbuf);
			list_del(&mbuf->ctx_link);
			kfree(mbuf);
			snps_accel_ctrl_priv_put(priv);
		}
	}
	mutex_unlock(&mem->list_lock);
}

/**
 * snps_accel_dma_direction - Get DMA data direction.
 * @dflags: Direction flags.
 *
 * Return: DMA data direction.
 */
static inline enum dma_data_direction snps_accel_dma_direction(u32 dflags)
{
	unsigned int ret = DMA_BIDIRECTIONAL;

	if (dflags == SNPS_ACCEL_IO_R)
		ret = DMA_FROM_DEVICE;

	if (dflags == SNPS_ACCEL_IO_W)
		ret = DMA_TO_DEVICE;

	return ret;
}

/**
 * snps_accel_dmabuf_create - Create DMA buffer.
 * @mem: Memory context.
 * @size: Size.
 * @dflags: Direction flags.
 *
 * Return: Address of DMA buffer on success, otherwise an appropriate error code.
 */
struct snps_accel_mem_buffer *
snps_accel_dmabuf_create(struct snps_accel_mem_ctx *mem, u64 size, u32 dflags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct snps_accel_mem_buffer *mbuf;
	enum dma_data_direction dma_dir;
	int ret;

	dma_dir = snps_accel_dma_direction(dflags);
	mbuf = snps_accel_mbuf_alloc(mem, size, dma_dir);
	if (!mbuf)
		return NULL;

	dev_dbg(mbuf->dev, "dmabuf create: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	exp_info.ops = &snps_accel_dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = mbuf;

	mbuf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(mbuf->dmabuf)) {
		snps_accel_mbuf_free(mem, mbuf, dma_dir);
		dev_err(mem->dev, "Failed to export DMA buffer: %ld\n",
			PTR_ERR(mbuf->dmabuf));
		return NULL;
	}

	mbuf->fd = dma_buf_fd(mbuf->dmabuf, O_ACCMODE | O_CLOEXEC);
	if (mbuf->fd < 0) {
		dma_buf_put(mbuf->dmabuf);
		dev_err(mem->dev, "Failed to get file descriptor: %d\n", ret);
		return NULL;
	}

	ret = snps_accel_dmabuf_attach_device(mbuf->dmabuf, mbuf->dev,
					      mbuf, mbuf->dma_dir);
	if (ret) {
		dma_buf_put(mbuf->dmabuf);
		dev_err(mem->dev,
			"Failed to attach dmabuf to device: %d\n", ret);
		return NULL;
	}

	return mbuf;
}

/**
 * snps_accel_get_dmabuf_info - Get information of DMA buffer.
 * @mem: Memory region context.
 * @info: Output information of DMA buffer.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_get_dmabuf_info(struct snps_accel_mem_ctx *mem,
			       struct snps_accel_dmabuf_info *info)
{
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;

	dmabuf = dma_buf_get(info->fd);
	if (IS_ERR(dmabuf)) {
		dev_err(mem->dev,
			"Failed to get DMA buffer by fd %d: %ld\n", info->fd,
			PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	/*
	 * Look up a DMA buffer explicitly so that it allows user-mode
	 * program to provide this driver with a file descriptor exported
	 * by another driver and imported into the this driver.
	 */
	mbuf = snps_accel_dmabuf_find_by_fd(mem, info->fd);
	if (!mbuf) {
		dma_buf_put(dmabuf);
		dev_err(mem->dev, "Failed to find DMA buffer by fd %d\n",
			info->fd);
		return -EINVAL;
	}

	info->addr = mbuf->da;
	info->size = mbuf->size;

	dev_dbg(mbuf->dev, "dmabuf info: va/pa/da %pS/%pa/%pad, size %zu\n",
		mbuf->va, &mbuf->pa, &mbuf->da, mbuf->size);

	dma_buf_put(dmabuf);

	return 0;
}

/**
 * snps_accel_dmabuf_release - Release DMA buffer.
 * @mbuf: Memory buffer.
 */
void snps_accel_dmabuf_release(struct snps_accel_mem_buffer *mbuf)
{
	dma_buf_put(mbuf->dmabuf);
}

/**
 * snps_accel_do_dmabuf_import - Import DMA buffer.
 * @mem: Memory context.
 * @fd: File descriptor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_do_dmabuf_import(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mem);
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;
	int ret;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		dev_err(mem->dev, "Failed to get DMA buffer by fd %d: %ld\n",
			fd, PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf) {
		dma_buf_put(dmabuf);
		ret = -ENOMEM;
		goto out_alloc;
	}

	mbuf->dma_dir = DMA_BIDIRECTIONAL;
	mbuf->dev = mem->dev;

	ret = snps_accel_dmabuf_attach_device(dmabuf, mem->dev,
					      mbuf, mbuf->dma_dir);
	if (ret) {
		dev_err(mem->dev, "Failed to attach DMA buffer: %d\n", ret);
		goto out_attach;
	}

	if (!snps_accel_dmabuf_is_contiguous(mbuf->dmasgt)) {
		dev_err(mem->dev,
			"DMA scatter-gather table is not contiguous.\n");
		ret = -EINVAL;
		goto out_contiguous;
	}

	mbuf->fd = fd;
	mbuf->dmabuf = dmabuf;
	mbuf->size = dmabuf->size;
	mbuf->va = NULL;

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_ctrl_priv_get(priv);

	return 0;

out_contiguous:
	dma_buf_unmap_attachment(mbuf->import_attach, mbuf->dmasgt,
				 mbuf->dma_dir);
	dma_buf_detach(dmabuf, mbuf->import_attach);

out_attach:
	kfree(mbuf);

out_alloc:
	dma_buf_put(dmabuf);
	return ret;
}

/**
 * snps_accel_do_dmabuf_detach - Detach DMA buffer.
 * @mem: Memory context.
 * @fd: File descriptor.
 *
 * Return: 0 on success, otherwise an appropriate error code.
 */
int snps_accel_do_dmabuf_detach(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_ctrl_priv *priv = to_snps_accel_ctrl_priv(mem);
	struct snps_accel_mem_buffer *mbuf;

	mbuf = snps_accel_dmabuf_find_by_fd(mem, fd);
	if (!mbuf) {
		dev_err(mem->dev,
			"Failed to find imported dmabuf with fd %d\n", fd);
		return -EINVAL;
	}

	if (mbuf->ctx == NULL) {
		/* DMA buffer is not allocated, but imported by us */
		snps_accel_dmabuf_detach_device(mbuf);

		mutex_lock(&mem->list_lock);
		list_del(&mbuf->ctx_link);
		mutex_unlock(&mem->list_lock);

		kfree(mbuf);
		snps_accel_ctrl_priv_put(priv);
	}

	return 0;
}
