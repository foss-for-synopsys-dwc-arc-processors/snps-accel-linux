# SPDX-License-Identifier: GPL-2.0-only

snps_accel-y :=			\
	snps_accel_cfg.o	\
	snps_accel_drv.o	\
	snps_accel_mbox.o	\
	snps_accel_mem.o	\
	snps_accel_mgr.o	\
	snps_accel_pm.o		\
	snps_accel_proc.o	\
	snps_accel_sysfs.o
obj-$(CONFIG_DRM_ACCEL_SNPS) += snps_accel.o

ccflags-$(CONFIG_DRM_ACCEL_SNPS_DEBUG) += -DDEBUG
