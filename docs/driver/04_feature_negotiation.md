# Feature Negotiation

Recall from virtio documentation chapter 3.1.1, we have only covered the first three steps. In this section, we will cover the **next three steps** below:

- Read device feature bits, and write the subset of feature bits understood by the OS and driver to the device. During this step the driver MAY read (but MUST NOT write) the device-specific configuration fields to check that it can support the device before accepting it.
- Set the FEATURES_OK status bit. The driver MUST NOT accept new feature bits after this step. 
- Re-read device status to ensure the FEATURES_OK bit is still set: otherwise, the device does not support our subset of features and the device is unusable. 

However, because we are in legacy interface, the last two steps will be omitted. 

During feature negotiation, the guest (driver) will write a subset of feature bits, then send it to the host (device) to negotiate for such feature. In our case, for now, we only need `VIRTIO_NET_F_MAC` feature for MAC address. Therefore, we will only send that. 

The code for feature negotiation can be found below:

```c
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
```

## FAQ

**1. Why do we even need to negotiate features at the first place?** 

Some features offered by the host (device) may perform 'edit' operation to existing data. As the guest (driver), you can either accept this feature or not; depending on your decision, the behavior is different. 

**2. Why do you need to perform `guest_feat & host_feat`?** 

According to **2.2.1 Driver Requirements: Feature Bits**, the following is mentioned. 

> The driver MUST NOT accept a feature which the device did not offer, and MUST NOT accept a feature which requires another feature which was not accepted. 

For all the features the guest (driver) request, you need to filter it out accordin to what the host (device) offer. 