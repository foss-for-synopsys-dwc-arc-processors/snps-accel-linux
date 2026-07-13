# SPDX-License-Identifier: GPL-2.0-only

KDIR ?= $(KERNEL_DIR)
M := $(realpath $(CURDIR))

ifeq ($(strip $(KDIR)),)
	$(error KDIR is empty or not set)
endif

# Out-of-tree builds do not run this repo's Kconfig menu automatically.
# Enable module configs by default, but allow users to override on command line.
SNPS_CONFIG ?= \
	CONFIG_SNPS_ARCSYNC=m \
	CONFIG_SNPS_ACCEL_APP=m \
	CONFIG_SNPS_ACCEL_RPROC=m

# Directory used by the flat gather install target.
OUTPUT_DIR ?= $(M)/build/modules
# INSTALL_MOD_PATH= ?= $(OUTPUT_DIR)

all: modules
default: modules

modules:
	$(MAKE) -C "$(KDIR)" M="$(M)" $(SNPS_CONFIG) modules

modules_install: modules
	$(MAKE) -C "$(KDIR)" M="$(M)" $(SNPS_CONFIG) modules_install

# Gather all built modules into a single flat output directory.
install: modules
	@echo "Gathering modules into $(OUTPUT_DIR)"
	@mkdir -p "$(OUTPUT_DIR)"
	@find "$(M)" -path "$(OUTPUT_DIR)" -prune -o -type f -name "*.ko" -exec cp -f {} "$(OUTPUT_DIR)/" \;
	@echo "Flat module output:"
	@ls -1 "$(OUTPUT_DIR)"

clean:
	$(MAKE) -C "$(KDIR)" M="$(M)" clean
	rm -rf *.symvers *.order
	rm -rf "$(OUTPUT_DIR)"
	rm -rf build

.PHONY: all default modules modules_install install clean
