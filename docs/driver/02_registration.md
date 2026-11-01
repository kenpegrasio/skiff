# Registration

In this section, the main goal is to be able to set the *kernel driver in use* in `lspci -k` to our driver module, `skiff`. Achieving this behavior indicates that we have successfully bind our driver. 

There are two important sources for this section: `linux-6.18.58/Documentation/PCI/pci.rst` and `linux-6.18.58/include/linux/pci.h`. We will walk through this documentation and code step by step as we build on our driver. 

## Unbind `virtio-pci`

Before developing driver code, observe that the current `lspci -k` will show the *kernel driver in use* for **00:04.0 Ethernet controller: Red Hat, Inc. Virtio network device** to be `virtio-pci`. 

Therefore, we will first need to unbind and ask the kernel to search for driver (after we are done implementing). The command for this can be found below:

```bash
DEV=0000:00:04.0

# 1. unbind: take the card away from virtio-pci
echo $DEV | sudo tee /sys/bus/pci/drivers/virtio-pci/unbind

# 2. override: from now on, only a driver named "skiff" may take this card
echo skiff | sudo tee /sys/bus/pci/devices/$DEV/driver_override

# 3. re-probe: ask the kernel to find a driver for the card again
echo $DEV | sudo tee /sys/bus/pci/drivers_probe
```

## Device Discovery

From the documentation, the following is mentioned:

> PCI drivers "discover" PCI devices in a system via pci_register_driver().

This means that we need to call `pci_register_driver()` when initializing our driver. 

The semantic for the function call is also described in the documentation:

> PCI device drivers call ``pci_register_driver()`` during their initialization with a pointer to a structure describing the driver (``struct pci_driver``)

We can find this structure inside `include/linux/pci.h`; the structure is as follows (line 980 to 996):

```c
struct pci_driver {
	const char		*name;
	const struct pci_device_id *id_table;	/* Must be non-NULL for probe to be called */
	int  (*probe)(struct pci_dev *dev, const struct pci_device_id *id);	/* New device inserted */
	void (*remove)(struct pci_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	int  (*suspend)(struct pci_dev *dev, pm_message_t state);	/* Device suspended */
	int  (*resume)(struct pci_dev *dev);	/* Device woken up */
	void (*shutdown)(struct pci_dev *dev);
	int  (*sriov_configure)(struct pci_dev *dev, int num_vfs); /* On PF */
	int  (*sriov_set_msix_vec_count)(struct pci_dev *vf, int msix_vec_count); /* On PF */
	u32  (*sriov_get_vf_total_msix)(struct pci_dev *pf);
	const struct pci_error_handlers *err_handler;
	const struct attribute_group **groups;
	const struct attribute_group **dev_groups;
	struct device_driver	driver;
	struct pci_dynids	dynids;
	bool driver_managed_dma;
};
```

As defined, `name` is a `const char*` meaning that we can pass in raw directly using `""`; `id_table` is an array of `pci_device_id`. 

## `id_table`

Below the `struct`, we are given a macro to help us construct `pci_device_id` (line 1002 to 1013, 1044 to 1056). 

```c
/**
 * PCI_DEVICE - macro used to describe a specific PCI device
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device.  The subvendor and subdevice fields will be set to
 * PCI_ANY_ID.
 */
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_DEVICE_SUB - macro used to describe a specific PCI device with subsystem
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 * @subvend: the 16 bit PCI Subvendor ID
 * @subdev: the 16 bit PCI Subdevice ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device with subsystem information.
 */
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = (subvend), .subdevice = (subdev)
```

From `lspci -nn`, we can found the vendor and device of the hardware, which is `1af4` and `1000` respectively. By inspecting the output of `lspci -nn`, we can deduce this. 

```
00:04.0 Ethernet controller [0200]: Red Hat, Inc. Virtio network device [1af4:1000]
00:05.0 SCSI storage controller [0100]: Red Hat, Inc. Virtio block device [1af4:1001]
00:06.0 SCSI storage controller [0100]: Red Hat, Inc. Virtio block device [1af4:1001]
```

The same vendor, i.e., *Red Hat, Inc.*, also provides the SCSI storage controller with a different device code, `1001`. 

> **Notes**: Observe that the macro only expands the attributes of the structure without the `{}`. We need to add it ourselves! 

From the documentation, the following is also mentioned:

> The ID table is an array of ``struct pci_device_id`` entries ending with an
all-zero entry.

It is important for us to also add an all-zero entry to the very back. We will use the `PCI_DEVICE_SUB` for this because `PCI_ANY_ID` is `(~0)`. 

The final code for this ID table can be found below:

```c
static const struct pci_device_id virtio_pci_tbl[] = {
    { PCI_DEVICE(0x1af4, 0x1000) }, // this comes from lspci -nn
    { PCI_DEVICE_SUB(0, 0, 0, 0) }
};
```

## Probe and Remove

Next, we will also provide the probe and remove functions with matching function signature as given in the `pci_driver` structure. 

For the probe function, the following is mentioned in the comments above `pci_driver` structure. 

> The probe function returns zero when the driver chooses to take "ownership" of the device or an error code (negative number) otherwise.

We need to return 0 from the function. The code for the probe and remove can be found below. 

```c
static int skiff_probe(struct pci_dev *dev, const struct pci_device_id *id) 
{
    pr_info("Probe from Skiff\n");
    return 0;
}

static void skiff_remove(struct pci_dev *dev) 
{
    pr_info("Remove from Skiff\n");
}
```
