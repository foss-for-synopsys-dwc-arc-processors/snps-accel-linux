# ARC NPU Linux Drivers

This repository contains ARC VPX/NPX accelerator kernel drivers:
- `snps_arcsync` (synchronization/control support)
- `snps_accel_app` (application helper interface)
- `snps_accel_rproc` (remoteproc support)

The top-level `Makefile`/`Kbuild` supports building these drivers as out-of-tree
modules against a vanilla Linux kernel build directory.

## Out-of-Tree Build

### 1) Build environment

Example cross-toolchain setup using a pre-built ARM GNU toolchain:

```bash
export PATH=$PATH:/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin
export CROSS_COMPILE=aarch64-none-linux-gnu-
export ARCH=arm64
```

Kernel paths from your setup:
- Kernel source/build directory: `linux`

### 2) Build modules

Set the `KERNEL_DIR` environment variable to your kernel directory. This step is
optional: the `Makefile` assigns `KERNEL_DIR` to `KDIR`, so you can set
`KERNEL_DIR` once instead of passing `KDIR` on every `make` command line.

```bash
export KERNEL_DIR=/path/to/linux
```

If you use separate kernel source and output directories, set `KERNEL_DIR` to your
build output directory.

From `snps-accel-linux`:

```bash
make
```

This command builds all modules in the out-of-tree repository.

Even if `KERNEL_DIR` is already set, you can pass `KDIR` explicitly on the
`make` command line:

```bash
make KDIR=/path/to/linux-6.6
```

By default, the top-level `Makefile` passes:
`CONFIG_SNPS_ARCSYNC=m CONFIG_SNPS_ACCEL_APP=m CONFIG_SNPS_ACCEL_RPROC=m`.

To build only selected modules, override config values on the command line, for example:

```bash
make KDIR=/path/to/linux-6.6 \
     SNPS_CONFIG="CONFIG_SNPS_ARCSYNC=m CONFIG_SNPS_ACCEL_APP=n CONFIG_SNPS_ACCEL_RPROC=m" \
     install
```

Prerequisite: the target kernel config must enable loadable modules
(`CONFIG_MODULES=y`), otherwise external module builds are rejected by Kbuild.

### 3) Standard kernel modules install (`modules_install`)

This uses the kernel default install layout under:
`<INSTALL_MOD_PATH>/lib/modules/<kernel-release>/...`

```bash
make KDIR=/path/to/linux \
     modules_install INSTALL_MOD_PATH=/tmp/linux-build-mod-install
```

`modules_install` works best when `INSTALL_MOD_PATH` points to a tree that already
contains modules installed during your Linux kernel build. That layout lets
`modprobe` resolve dependencies when loading the drivers.

### 4) Flat gather install (`install`)

Use this to collect all built `*.ko` files into one directory:

```bash
make install
```

This command builds all modules in the out-of-tree repository and copies them into
`build/modules`. You can then copy the modules to your target system and load them
with `insmod`.

The install path can be overridden with the `OUTPUT_DIR` build variable:

```bash
make KDIR=/path/to/linux \
     OUTPUT_DIR=/new/path/for/modules install
```

## In-Tree Integration (Vanilla Kernel Tree)

To integrate these drivers directly in the kernel source tree:

### 1) Copy driver directories into kernel tree

From kernel source root:

```bash
cp -r /path/to/snps-accel-linux/drivers/misc/snps_accel drivers/misc/
cp -r /path/to/snps-accel-linux/drivers/remoteproc/snps_accel drivers/remoteproc/
```

### 2) Hook Kconfig entries

In `drivers/misc/Kconfig`, add:

```kconfig
source "drivers/misc/snps_accel/Kconfig"
```

In `drivers/remoteproc/Kconfig`, add:

```kconfig
source "drivers/remoteproc/snps_accel/Kconfig"
```

### 3) Hook driver subdirectories into build

In `drivers/misc/Makefile`, add:

```makefile
obj-y += snps_accel/
```

In `drivers/remoteproc/Makefile`, add:

```makefile
obj-y += snps_accel/
```

### 4) Configure and build

Use `menuconfig` and select `SNPS_ARCSYNC`, `SNPS_ACCEL_APP`, and `SNPS_ACCEL_RPROC`
as built-in (`y`) or module (`m`):

```bash
make menuconfig
make -j$(nproc) Image modules
```
