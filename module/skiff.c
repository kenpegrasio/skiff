// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/pci.h> 
#include <linux/io.h>

#include <linux/virtio_pci.h>
#include <linux/virtio_config.h>
#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h> 

#define ALIGNMENT 4096

struct skiff_tx_reclaim {
    struct sk_buff *skb;
    dma_addr_t dma_handle;
    unsigned int len;
};

struct skiff_dev 
{
    void __iomem *ioaddr;
    void *cpu_addr_0;
    void *cpu_addr_1;
    unsigned int queue_size_0;
    unsigned int queue_size_1;
    // TX
    struct vring v1;
    unsigned int free_desc_idx;
    dma_addr_t net_hdr_addr;
    // TX clean up
    unsigned int last_seen_used;
    struct skiff_tx_reclaim *reclaim_arr;
};

static inline unsigned align(unsigned int x) 
{
    return (unsigned int) ((x + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
}

static inline unsigned virtq_size(unsigned int qsz) 
{ 
    return align(16 * qsz + 6 + 2 * qsz) + align(6 + 8 * qsz); 
}

static int skiff_open(struct net_device *dev)
{
    return 0;
}

static int skiff_close(struct net_device *dev)
{
    return 0;
}

static netdev_tx_t skiff_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
    struct device *ddev = netdev->dev.parent;
	void *addr = skb->data;
	size_t size = skb->len;

    struct skiff_dev *priv = netdev_priv(netdev);

    // Reclaim from virtqueue used ring
    while (true) {
        unsigned int cur_used_idx = READ_ONCE(priv->v1.used->idx);
        if (priv->last_seen_used == cur_used_idx) { // no need endianness conversion because of 2.7.3
            break;
        }
        dma_rmb();
        unsigned int i = (priv->v1.used->ring[priv->last_seen_used % priv->queue_size_1].id + 1) % priv->queue_size_1;
        dma_unmap_single(ddev, priv->reclaim_arr[i].dma_handle, priv->reclaim_arr[i].len, DMA_TO_DEVICE);
        dev_consume_skb_any(priv->reclaim_arr[i].skb);
        DEV_STATS_INC(netdev, tx_packets);
        DEV_STATS_ADD(netdev, tx_bytes, priv->reclaim_arr[i].len);
        priv->reclaim_arr[i].skb = NULL;
        priv->last_seen_used = (priv->last_seen_used + 1) % 65536;
    }

    unsigned int start_desc_idx = priv->free_desc_idx;
    unsigned int next_idx = (priv->free_desc_idx + 1) % priv->queue_size_1;

    if (priv->reclaim_arr[next_idx].skb != NULL) {
        DEV_STATS_INC(netdev, tx_dropped);
        dev_err_ratelimited(ddev, "No reclaim yet, descriptor ring is full\n");
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    } 

    // 'Send' the buffer to the DMA such that the device can have access to the buffer
	dma_addr_t dma_handle = dma_map_single(ddev, addr, size, DMA_TO_DEVICE);
    if (dma_mapping_error(ddev, dma_handle)) 
    {
        DEV_STATS_INC(netdev, tx_dropped); // to show status in ip -s link
        dev_err_ratelimited(ddev, "Failed to allocate DMA streaming memory\n");
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    // 2.7.13 Step 1 (detail is on 2.7.13.1)
    // Requirement from 5.1.6.6
    priv->v1.desc[priv->free_desc_idx].addr = priv->net_hdr_addr;
    priv->v1.desc[priv->free_desc_idx].len = sizeof(struct virtio_net_hdr);
    priv->v1.desc[priv->free_desc_idx].flags = VRING_DESC_F_NEXT;
    priv->v1.desc[priv->free_desc_idx].next = (priv->free_desc_idx + 1) % priv->queue_size_1; // next is used to chain descriptors
    // Send the actual buffer
    priv->v1.desc[(priv->free_desc_idx + 1) % priv->queue_size_1].addr = dma_handle;
    priv->v1.desc[(priv->free_desc_idx + 1) % priv->queue_size_1].len = size;
    priv->v1.desc[(priv->free_desc_idx + 1) % priv->queue_size_1].flags = 0;
    // Save pointer for clean up
    priv->reclaim_arr[(priv->free_desc_idx + 1) % priv->queue_size_1].skb = skb;
    priv->reclaim_arr[(priv->free_desc_idx + 1) % priv->queue_size_1].dma_handle = dma_handle;
    priv->reclaim_arr[(priv->free_desc_idx + 1) % priv->queue_size_1].len = size;

    // 2.7.13 Step 2
    priv->v1.avail->ring[priv->v1.avail->idx % priv->queue_size_1] = start_desc_idx;

    // 2.7.13 Step 4
    dma_wmb();

    // 2.7.13 Step 5
    priv->v1.avail->idx++; // number of descriptor head = 1
    priv->free_desc_idx = (priv->free_desc_idx + 2) % priv->queue_size_1;

    // 2.7.13 Step 6

    // 2.7.13 Step 7 (detail in 4.1.5.2)
    void __iomem *queue_notify_loc = priv->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY;
    iowrite16(1, queue_notify_loc);

    return NETDEV_TX_OK;
}

static int skiff_probe(struct pci_dev *dev, const struct pci_device_id *id) 
{
    struct skiff_dev *priv;
    struct net_device *net_dev = devm_alloc_etherdev(&dev->dev, sizeof(struct skiff_dev));
    if (net_dev == NULL) {
        dev_err(&dev->dev, "Failed to allocate net_device\n");
        return -ENOMEM;
    }
    pci_set_drvdata(dev, net_dev);
    priv = netdev_priv(net_dev);

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
    priv->ioaddr = pcim_iomap(dev, 0, 0);
    if (priv->ioaddr == NULL) {
        dev_err(&dev->dev, "Failed to map BAR0\n");
        return -ENOMEM;
    }
    dev_info(&dev->dev, "BAR0 mapped\n"); 

    void __iomem *status_loc = priv->ioaddr + VIRTIO_PCI_STATUS;
    iowrite8(0, status_loc); // reset
    u8 status = ioread8(status_loc);
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE; // set ACKNOWLEDGE
    iowrite8(status, status_loc);
    status |= VIRTIO_CONFIG_S_DRIVER; // set DRIVER
    iowrite8(status, status_loc);

    void __iomem *host_feature_loc = priv->ioaddr + VIRTIO_PCI_HOST_FEATURES;
    u32 host_feat = ioread32(host_feature_loc);
    unsigned long host_bits = host_feat;
    dev_info(&dev->dev, "Offered feature: %*pbl\n", 32, &host_bits);
    u32 guest_feat = 0;
    guest_feat |= (1u << VIRTIO_NET_F_MAC);
    void __iomem *guest_feature_loc = priv->ioaddr + VIRTIO_PCI_GUEST_FEATURES;
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

    // Set DMA mask (more detail on dma-api-howto.rst)
    // Coherent: 32-bit is the page number, the page size is 4096 that is why you >> 12
    if (dma_set_coherent_mask(&dev->dev, DMA_BIT_MASK(44))) {
        dev_err(&dev->dev, "No suitable DMA coherent available");
        return -EIO;
    }
    // Streaming: The address field in virtq_desc is 64 bits
    if (dma_set_mask(&dev->dev, DMA_BIT_MASK(64))) {
        dev_err(&dev->dev, "No suitable DMA streaming available");
        return -EIO;
    }

    // From 5.1.6.6, network data need to be preceeded with struct virtio_net_hdr
    // We need the struct to be in DMA such that the device can have access
    // Because it will always be the same, we will allocate DMA coherent memory
    dma_addr_t dma_handle_net_hdr;
    void *cpu_addr_net_hdr = dmam_alloc_coherent(&dev->dev, sizeof(struct virtio_net_hdr), &dma_handle_net_hdr, GFP_KERNEL);
    if (cpu_addr_net_hdr == NULL) {
        dev_err(&dev->dev, "Failed to allocate DMA coherent memory for net_hdr\n");
        return -ENOMEM;
    }
    priv->net_hdr_addr = dma_handle_net_hdr;

    // Virtqueues 
    void __iomem *queue_select_loc = priv->ioaddr + VIRTIO_PCI_QUEUE_SEL;
    void __iomem *queue_size_loc = priv->ioaddr + VIRTIO_PCI_QUEUE_NUM;
    void __iomem *queue_address_loc = priv->ioaddr + VIRTIO_PCI_QUEUE_PFN;

    // 0
    iowrite16(0, queue_select_loc);
    u16 queue_size_0 = ioread16(queue_size_loc);
    if (queue_size_0 == 0) {
        dev_err(&dev->dev, "Queue select 0 is empty\n");
        iowrite8(0, status_loc); // reset
        return -EIO;
    }
    priv->queue_size_0 = queue_size_0;
    dev_info(&dev->dev, "Queue 0 entries: %d\n", queue_size_0);
    unsigned int aligned_queue_size_0 = virtq_size(queue_size_0);
    dma_addr_t dma_handle_0;
    void *cpu_addr_0 = dmam_alloc_coherent(&dev->dev, aligned_queue_size_0, &dma_handle_0, GFP_KERNEL);
    if (cpu_addr_0 == NULL) {
        dev_err(&dev->dev, "Failed to allocate DMA coherent memory for queue 0\n");
        iowrite8(0, status_loc); // reset
        return -ENOMEM;
    }
    priv->cpu_addr_0 = cpu_addr_0;
    u32 addr_0 = dma_handle_0 >> 12;
    dev_info(&dev->dev, "DMA handle 0: %x\n", addr_0);
    iowrite32(addr_0, queue_address_loc);
    {
        u32 read_back = ioread32(queue_address_loc);
        dev_info(&dev->dev, "Queue address (read back): %x\n", read_back);
    }

    // 1: TX
    iowrite16(1, queue_select_loc);
    u16 queue_size_1 = ioread16(queue_size_loc);
    if (queue_size_1 == 0) {
        dev_err(&dev->dev, "Queue select 1 is empty\n");
        iowrite8(0, status_loc); // reset
        return -EIO;
    }
    priv->queue_size_1 = queue_size_1;
    dev_info(&dev->dev, "Queue 1 entries: %d\n", queue_size_1);
    unsigned int aligned_queue_size_1 = virtq_size(queue_size_1);
    dma_addr_t dma_handle_1;
    void *cpu_addr_1 = dmam_alloc_coherent(&dev->dev, aligned_queue_size_1, &dma_handle_1, GFP_KERNEL);
    if (cpu_addr_1 == NULL) {
        dev_err(&dev->dev, "Failed to allocate DMA coherent memory for queue 1\n");
        iowrite8(0, status_loc); // reset
        return -ENOMEM;
    }
    priv->cpu_addr_1 = cpu_addr_1;
    u32 addr_1 = dma_handle_1 >> 12;
    dev_info(&dev->dev, "DMA handle 1: %x\n", addr_1);
    iowrite32(addr_1, queue_address_loc);
    {
        u32 read_back = ioread32(queue_address_loc);
        dev_info(&dev->dev, "Queue address (read back): %x\n", read_back);
    }
    priv->free_desc_idx = 0;
    // Create vring
    vring_init(&priv->v1, priv->queue_size_1, priv->cpu_addr_1, ALIGNMENT);
    priv->v1.avail->flags = 1; // disable interrupt for now
    // Clean up metadata initialization
    priv->last_seen_used = 0;
    priv->reclaim_arr = devm_kcalloc(&dev->dev, queue_size_1, sizeof(struct skiff_tx_reclaim), GFP_KERNEL);
    if (priv->reclaim_arr == NULL) {
        dev_err(&dev->dev, "Failed to allocate memory for skiff_tx_reclaim\n");
        return -ENOMEM;
    }

    // Read and print MAC address
    if (used_feat & (1u << VIRTIO_NET_F_MAC)) {
        void __iomem *conf_loc = priv->ioaddr + VIRTIO_PCI_CONFIG_OFF(false);
        u8 mac_addr[6];
        for (int i = 0; i < 6; i++) {
            mac_addr[i] = ioread8(conf_loc);
            conf_loc++;
        }
        dev_info(&dev->dev, "Device mac address is %pM\n", mac_addr);
        eth_hw_addr_set(net_dev, mac_addr); // 5.1.4.2
                                            // If the driver negotiates the VIRTIO_NET_F_MAC feature, the driver MUST set the physical address of the NIC to mac
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

    static const struct net_device_ops skiff_netdev_ops = {
        .ndo_open = skiff_open,
        .ndo_stop = skiff_close,
        .ndo_start_xmit = skiff_start_xmit,
    };

    net_dev->netdev_ops = &skiff_netdev_ops;
    SET_NETDEV_DEV(net_dev, &dev->dev);
    int reg = register_netdev(net_dev);
    if (reg) 
    {
        dev_err(&dev->dev, "Failed to register network device\n");
        iowrite8(0, status_loc); // reset
        return reg;
    }
    netif_carrier_off(net_dev);
    netif_carrier_on(net_dev);
    // Whenever the driver CHANGES one of these flags, a workqueue event is
    // scheduled to translate the flag combination to IFLA_OPERSTATE
    return 0;
}

static void skiff_remove(struct pci_dev *dev) 
{
    struct net_device *net_dev = pci_get_drvdata(dev);
    struct skiff_dev *priv = netdev_priv(net_dev);

    unregister_netdev(net_dev);

    dev_info(&dev->dev, "Remove from Skiff\n");
    void __iomem *status_loc = priv->ioaddr + VIRTIO_PCI_STATUS;
    iowrite8(0, status_loc); // reset

    // Clean up buffer and DMA streaming memory
    for (int i = 0; i < priv->queue_size_1; i++) {
        if (priv->reclaim_arr[i].skb == NULL) continue;
        dma_unmap_single(&dev->dev, priv->reclaim_arr[i].dma_handle, priv->reclaim_arr[i].len, DMA_TO_DEVICE);
        dev_kfree_skb_any(priv->reclaim_arr[i].skb);
    }

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
