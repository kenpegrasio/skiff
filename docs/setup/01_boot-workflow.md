# Boot Workflow

## Normal dual boot

In standard dual boot workflow, we will first start by flashing a disk image (`.img`) file onto a USB stick. This disk image contains all the necessary file for a minimal computer to work. 

While installing, all the content inside the disk image will be copied to the computer's disk. Every boot process after that will find the GRUB (bootloader) in SSD and mount the Ubuntu partition from the SSD. 

## VM

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