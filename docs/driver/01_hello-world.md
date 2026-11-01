# Hello, World

In this first section, the main goal here is to build an out-of-tree kernel module that can be attached using `sudo insmod` and detached using `sudo rmmod` inside the kernel. A complete guide to build external modules can be found in `linux-6.18.58/Documentation/kbuild/modules.rst`. 

## `Kbuild`

To build the module, we will first need to specify the *recipe*. This *recipe* will be reflected on a Kbuild file. You can read in more detail on how to combine this with a `Makefile` as well in the complete guide. 

When building the driver module, you need to link the driver object (`.o` file) with the kernel module. You can specify it using the following command:

```bash
make -C ~/skiff-work/build M=$PWD
```

where `~/skiff-work/build` is the directory that contains built kernel, what you will found after running `build-kernel.sh`. 

> Notes: You will need to run this `make` command inside the `module` directory. 

To transfer the `.ko` file to the VM, you can use the `ssh-guest.sh` script as follows:

```bash
scripts/ssh-guest.sh 'cat > skiff.ko' < module/skiff.ko
```

## Inside the module

Overall, when `insmod` is run, `module_init` function will be invoked; when `rmmod` is run, `module_exit` function will be invoked. `__init` specifies that the function will only be invoked once in the initialization process, so the memory occupied by the function will be freed. 

> Notes: Empty parantheses in C function do not indicate that the function takes no argument, it indicates that the function takes an unspecified number of arguments. You need to add `void` to the argument when you want to specify that the function takes zero argument. 

## Commands

Inside guest:

```bash
sudo insmod skiff.ko # to attach driver module
sudo rmmod skiff.ko # to detach driver module
sudo dmesg | tail -3 # to print the kernel ring buffer (logs)
lsmod # to list all modules
```

Inside GDB:

```bash
lx-symbols ~/skiff/module/ # automatically (re-)load symbol files for main kernel and all loaded kernel modules
hbreak function_name # hardware breakpoint
break function_name # software breakpoint
```