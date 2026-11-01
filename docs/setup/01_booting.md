# Booting

## Boot Workflow

### Normal dual boot

In standard dual boot workflow, we will first start by flashing a disk image (`.img`) file onto a USB stick. This disk image contains all the necessary file for a minimal computer to work. 

While installing, all the content inside the disk image will be copied to the computer's disk. Every boot process after that will find the GRUB (bootloader) in SSD and mount the Ubuntu partition from the SSD. 

### VM

In this project, the normal dual boot process will not work. Whenever there is a bug, the whole computer will crash. You have no way to debug and need to reflash the new disk image every time. Because of this, we will use QEMU to help us. 

In this workflow, the idea is to let QEMU boot the disk image with our own build of the unmodified 6.18.58 source. The disk image that we use is `noble-server-cloudimg-amd64.img` which can be downloaded using the following command:

```bash
wget https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img
wget https://cloud-images.ubuntu.com/noble/current/SHA256SUMS
sha256sum -c --ignore-missing SHA256SUMS
```

The kernel source code that we based everything from is `linux-6.18.58`, the newest LTS kernel as of 25 October 2026. This can be downloaded by using the following command:

```bash
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.58.tar.xz
tar xf linux-6.18.58.tar.xz
```

When using QEMU, the idea is: the disk image is read only and all writes will go to an overlay called `qcow2`. The `qcow2` overlay will be created in the `make-guest.sh` together with the user information. 

After having the `qcow2`, we can then boot by running the QEMU using `run-qemu.sh`. 

## Boot Details

### `qcow2` 

By definition, `qcow2` stands for QEMU Copy-On-Write, version 2. In short, it is a native disk image format for QEMU that allows VM storage to grow dynamically as data comes in. 

The virtual maximum size of `qcow2` is written in the header when you run the `qemu-img create` command. 

> **Notes**: `qemu-img create` does not partition anything inside the host laptop. It only set the maximum virtual size in the `qcow2` file header. 

The `qcow2` file will grow as you perform write operations (for example, install package) inside the VM. Inside the VM, the disk partition will be set according to the virtual maximum size.

Interestingly, the `qcow2` at first will not directly occupy the virtual maximum size, it will only grow when you perform write operations. 

### Disk Image (`.img`) 

Disk image has been mentioned a lot of time. A one-million dollar question to ask is: **what does this disk image contain**?

The disk image contains the following:
- Bootloader
- Compiled kernel
- All essential packages and programs (like firefox, text editor, etc) 

In short, it contains all the foundational building blocks for a minimal laptop to work. 

> **Device Drivers**. Kernel inside has many device drivers code. How does this work? Each hardware model has a unique ID. This ID is used by the kernel to identify which device driver code to run. 

An interesting to ponder subsequently is: **how do you organize all these files (kernel source code, packages, programs) inside the disk image**?

This is where **filesystem** concepts comes in. When mentioning filesystem here, we are not referring to the data Instead, we will focus on the organization of the data. 

Every data inside the filesystem will be stored inside the physical disk. The job of the kernel here is to interpret what type of filesystem and how the data is organized. The type of the filesystem is reflected by a magic number inside a specific bytes location. Then, to read and write, the kernel will call the appropriate device driver code to interact with the hardware (disk).

Let us then explore how the `noble-server-cloudimg-amd64.img` is organized.

```bash
$ mkdir -p ~/skiff-work/tmp && cd ~/skiff-work/tmp
$ IMG=~/skiff-work/images/noble-server-cloudimg-amd64.img
$ qemu-img info $IMG

image: /home/jansen-ken-pegrasio/skiff-work/images/noble-server-cloudimg-amd64.img
file format: qcow2
virtual size: 3.5 GiB (3758096384 bytes)
disk size: 593 MiB
cluster_size: 65536
Format specific information:
    compat: 1.1
    compression type: zlib
    lazy refcounts: false
    refcount bits: 16
    corrupt: false
    extended l2: false
Child node '/file':
    filename: /home/jansen-ken-pegrasio/skiff-work/images/noble-server-cloudimg-amd64.img
    protocol type: file
    file length: 593 MiB (621284352 bytes)
    disk size: 593 MiB
```

From the output above, observe that virtual size and disk size are not the same. The filesystem can contain up to 3.5 GiB, but in reality only the non empty space is stored inside the image itself. 

```bash
$ qemu-img convert -O raw $IMG noble.raw
$ fdisk -l noble.raw

Disk noble.raw: 3.5 GiB, 3758096384 bytes, 7340032 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: gpt
Disk identifier: E14D0243-D31D-4718-846E-D99094E48A2A

Device        Start     End Sectors  Size Type
noble.raw1  2099200 7339998 5240799  2.5G Linux filesystem
noble.raw14    2048   10239    8192    4M BIOS boot
noble.raw15   10240  227327  217088  106M EFI System
noble.raw16  227328 2097152 1869825  913M Linux extended boot

Partition table entries are not in disk order.
```