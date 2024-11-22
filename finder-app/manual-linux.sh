#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]     # If #args < 1...
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}      # Make parents if needed...

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then                      # "! -d" means if dir does not exist
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then        # If file does not exist...
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs

fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var 
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
	make defconfig

else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH}  CROSS_COMPILE=${CROSS_COMPILE} 
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

# busybox: setuid root?
sudo chown root ${OUTDIR}/rootfs/bin/busybox
sudo chmod 4555 ${OUTDIR}/rootfs/bin/busybox


echo "Library dependencies"
${CROSS_COMPILE}readelf -a busybox | grep "program interpreter"
# aarch64-none-linux-gnu-readelf -a bin/busybox | grep "program interpreter"
#       /lib/ld-linux-aarch64.so.1
${CROSS_COMPILE}readelf -a busybox | grep "Shared library"
# aarch64-none-linux-gnu-readelf -a bin/busybox | grep "Shared library"
# libm.so.6  libresolv.so.2  libc.so.6

# # TODO: Add library dependencies to rootfs
cd ${OUTDIR}/rootfs
# SYSROOT=/home/flatos/Projects/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin/../aarch64-none-linux-gnu/libc
# cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 lib
# cp ${SYSROOT}/lib64/libm.so.6 lib64
# cp ${SYSROOT}/lib64/libresolv.so.2 lib64
# cp ${SYSROOT}/lib64/libc.so.6 lib64
cp ${FINDER_APP_DIR}/lib/ld-linux-aarch64.so.1 lib
cp ${FINDER_APP_DIR}/lib64/libm.so.6 lib64
cp ${FINDER_APP_DIR}/lib64/libresolv.so.2 lib64
cp ${FINDER_APP_DIR}/lib64/libc.so.6 lib64


# # TODO: Make device nodes
cd ${OUTDIR}/rootfs
sudo mknod  -m 666 dev/null c 1 3
sudo mknod  -m 666 dev/console c 5 1

# # TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=aarch64-none-linux-gnu-


# # TODO: Copy the finder related scripts and executables to the /home directory
# # on the target rootfs
mkdir -p ${OUTDIR}/rootfs/home
cp finder-test.sh finder.sh writer writer.sh ${OUTDIR}/rootfs/home
cp autorun-qemu.sh ${OUTDIR}/rootfs/home
# mkdir -p ${OUTDIR}/rootfs/home/conf
cp -r ../conf ${OUTDIR}/rootfs/home

# # TODO: Chown the root directory
sudo chown root:root ${OUTDIR}/rootfs

# # TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H  newc -ov  --owner root:root > $OUTDIR/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio



