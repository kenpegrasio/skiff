// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/pci.h> 

static int skiff_probe(struct pci_dev *dev, const struct pci_device_id *id) 
{
    pr_info("Probe from Skiff\n");
    return 0;
}

static void skiff_remove(struct pci_dev *dev) 
{
    pr_info("Remove from Skiff\n");
}

static const struct pci_device_id virtio_pci_tbl[] = {
    { PCI_DEVICE(0x1af4, 0x1000) }, // this comes from lspci -nn
    { PCI_DEVICE_SUB(0, 0, 0, 0) }
};

static struct pci_driver skiff_driver = {
    .name = "skiff",
    .id_table = virtio_pci_tbl,
    .probe = skiff_probe,
    .remove = skiff_remove,
};

static int __init skiff_init(void);
static int skiff_init(void) 
{
    pr_info("Hello, world!\n");
    return pci_register_driver(&skiff_driver); // error from register should also produce error in init
}

static void __exit skiff_exit(void);
static void skiff_exit(void) 
{
    pr_info("Exit from Skiff\n");
    pci_unregister_driver(&skiff_driver);
}

module_init(skiff_init);
module_exit(skiff_exit);

MODULE_LICENSE("GPL");
