# Building Blocks

## GDB

GDB stands for GNU Project Debugger. The most useful thing is: it allows you to freeze the VM and backtrace the state of the CPU. 

Useful commands in gdb:

```bash
info threads     # one "thread" per virtual CPU; you should see 2
bt               # backtrace: the chain of functions that led to where CPU 0 is now
lx-dmesg         # the guest's kernel log, read directly out of its memory
continue         # unfreeze the VM
```

Other commands: Ctrl + C to freeze again, quit to detach the GDB. 

In our project, to attach the GDB to the guest, you can run the `gdb-guest.sh` script. 

## Tap Interface

On a normal machine, the OS talks to real hardware. In contrast, VM is a sealed box; the guest kernel never touches the real devices because a VM's hardware is emulated by QEMU. Hence, anything a virtual card sends or receives has to go through something the host provides. Because of this, every virtual card needs a host-side endpoint to be useful; otherwise, it will just an isolated card with no way in and no way out. 

For this project, we create that endpoint, `skiff0`, a virtual (tap) interface on the host using `tap-up.sh` script. When QEMU starts, it connects the guest's virtio-net card to `skiff0`, which makes a private cable between guest and host. Frames the driver sends come out of skiff0 on the host, and frames the host sends to skiff0 arrive at the guest's card. No physical hardware is involved.

To check for the connection, we run `lspci -nn` in the guest and `ip -br link show skiff0` in the host and check whether virtio network device exists or not. 