// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/pci.h> 
#include <linux/io.h>

#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_net.h>

void __iomem *ioaddr;

static int skiff_probe(struct pci_dev *dev, const struct pci_device_id *id) 
{
    dev_info(&dev->dev, "Probe from Skiff\n");
    int ena = pcim_enable_device(dev); // see pci.rst line 197
    if (ena != 0) {
        dev_err(&dev->dev, "Failed to enable device\n");
        return ena;
    }
    int req = pcim_request_region(dev, 0, "skiff"); 
        // see pci.rst line 246 and https://docs.kernel.org/driver-api/pci/pci.html#c.pci_request_region
    if (req != 0) {
        dev_err(&dev->dev, "Failed to request BAR0 region\n");
        return req;
    }
    ioaddr = pcim_iomap(dev, 0, 0);
    if (ioaddr == NULL) {
        dev_err(&dev->dev, "Failed to map BAR0\n");
        return -ENOMEM;
    }
    dev_info(&dev->dev, "BAR0 mapped\n"); 

    void __iomem *status_loc = ioaddr + VIRTIO_PCI_STATUS;
    iowrite8(0, status_loc); // reset
    u8 status = ioread8(status_loc);
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE; // set ACKNOWLEDGE
    iowrite8(status, status_loc);
    status |= VIRTIO_CONFIG_S_DRIVER; // set DRIVER
    iowrite8(status, status_loc);

    void __iomem *host_feature_loc = ioaddr + VIRTIO_PCI_HOST_FEATURES;
    u32 host_feat = ioread32(host_feature_loc);
    unsigned long host_bits = host_feat;
    dev_info(&dev->dev, "Offered feature: %*pbl\n", 32, &host_bits);
    u32 guest_feat = 0;
    guest_feat |= (1u << VIRTIO_NET_F_MAC);
    void __iomem *guest_feature_loc = ioaddr + VIRTIO_PCI_GUEST_FEATURES;
    u32 used_feat = guest_feat & host_feat;
    unsigned long used_bits = used_feat;
    iowrite32(used_feat, guest_feature_loc);
    dev_info(&dev->dev, "Accepted feature: %*pbl\n", 32, &used_bits);

    {
        u32 read_back = ioread32(guest_feature_loc);
        unsigned long read_back_bits = read_back;
        dev_info(&dev->dev, "Read back feature: %*pbl\n", 32, &read_back_bits);
    }

    if (used_feat & (1u << VIRTIO_NET_F_MAC)) {
        void __iomem *conf_loc = ioaddr + VIRTIO_PCI_CONFIG_OFF(false);
        u8 mac_addr[6];
        for (int i = 0; i < 6; i++) {
            mac_addr[i] = ioread8(conf_loc);
            conf_loc++;
        }
        dev_info(&dev->dev, "Device mac address is %pM\n", mac_addr);
    } else {
        dev_info(&dev->dev, "VIRTIO_NET_F_MAC is not set\n");
    }
    return 0;
}

static void skiff_remove(struct pci_dev *dev) 
{
    dev_info(&dev->dev, "Remove from Skiff\n");
    void __iomem *status_loc = ioaddr + VIRTIO_PCI_STATUS;
    iowrite8(0, status_loc); // reset
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
