// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/pci.h> 
#include <linux/io.h>

#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_net.h>

#define ALIGNMENT 4096

void __iomem *ioaddr;

static inline unsigned align(unsigned int x) 
{
    return (unsigned int) ((x + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
}

static inline unsigned virtq_size(unsigned int qsz) 
{ 
    return align(16 * qsz + 6 + 2 * qsz) + align(6 + 8 * qsz); 
}

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

    // Enables bus-mastering
    pci_set_master(dev);

    // Set DMA mask 
    // 32-bit is the page number, the page size is 4096 that is why you >> 12
    if (dma_set_coherent_mask(&dev->dev, DMA_BIT_MASK(44))) {
        dev_err(&dev->dev, "No suitable DMA available");
        return -EIO;
    }

    // Virtqueues 
    void __iomem *queue_select_loc = ioaddr + VIRTIO_PCI_QUEUE_SEL;
    void __iomem *queue_size_loc = ioaddr + VIRTIO_PCI_QUEUE_NUM;
    void __iomem *queue_address_loc = ioaddr + VIRTIO_PCI_QUEUE_PFN;

    // 0
    iowrite16(0, queue_select_loc);
    u16 queue_size_0 = ioread16(queue_size_loc);
    if (queue_size_0 == 0) {
        dev_err(&dev->dev, "Queue select 0 is empty\n");
        return -EIO;
    }
    dev_info(&dev->dev, "Queue 0 entries: %d\n", queue_size_0);
    unsigned int aligned_queue_size_0 = virtq_size(queue_size_0);
    dma_addr_t dma_handle_0;
    void *cpu_addr_0 = dmam_alloc_coherent(&dev->dev, aligned_queue_size_0, &dma_handle_0, GFP_KERNEL);
    if (cpu_addr_0 == NULL) {
        dev_err(&dev->dev, "Failed to allocate DMA coherent memory for queue 0\n");
        return -ENOMEM;
    }
    u32 addr_0 = dma_handle_0 >> 12;
    dev_info(&dev->dev, "DMA handle 0: %x\n", addr_0);
    iowrite32(addr_0, queue_address_loc);
    {
        u32 read_back = ioread32(queue_address_loc);
        dev_info(&dev->dev, "Queue address (read back): %x\n", read_back);
    }

    // 1
    iowrite16(1, queue_select_loc);
    u16 queue_size_1 = ioread16(queue_size_loc);
    if (queue_size_1 == 0) {
        dev_err(&dev->dev, "Queue select 1 is empty\n");
        iowrite8(0, status_loc); // reset
        return -EIO;
    }
    dev_info(&dev->dev, "Queue 1 entries: %d\n", queue_size_1);
    unsigned int aligned_queue_size_1 = virtq_size(queue_size_1);
    dma_addr_t dma_handle_1;
    void *cpu_addr_1 = dmam_alloc_coherent(&dev->dev, aligned_queue_size_1, &dma_handle_1, GFP_KERNEL);
    if (cpu_addr_1 == NULL) {
        dev_err(&dev->dev, "Failed to allocate DMA coherent memory for queue 1\n");
        iowrite8(0, status_loc); // reset
        return -ENOMEM;
    }
    u32 addr_1 = dma_handle_1 >> 12;
    dev_info(&dev->dev, "DMA handle 1: %x\n", addr_1);
    iowrite32(addr_1, queue_address_loc);
    {
        u32 read_back = ioread32(queue_address_loc);
        dev_info(&dev->dev, "Queue address (read back): %x\n", read_back);
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

    // Set DRIVER_OK
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    iowrite8(status, status_loc);
    {
        u8 read_back = ioread8(status_loc);
        dev_info(&dev->dev, "Read back status: %x\n", read_back);
    }
    return 0;
}

static void skiff_remove(struct pci_dev *dev) 
{
    dev_info(&dev->dev, "Remove from Skiff\n");
    void __iomem *status_loc = ioaddr + VIRTIO_PCI_STATUS;
    iowrite8(0, status_loc); // reset

    pci_clear_master(dev);
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
