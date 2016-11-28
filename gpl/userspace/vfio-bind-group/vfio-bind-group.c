#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/vfio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char buf[1024];

int is_pci_addr(const char *addr)
{
    return (addr[2] == ':' && addr[5] == '.' && addr[7] == '\0');
}

void usage(const char *name)
{
    printf("usage: %s <pci_addr> [pci_addr [pci_addr...]]\n", name);
    printf("\n");
    printf("Bind several devices into a VFIO group.\n");
    printf("\n");
    printf("If only one device is specified, only dump device info.\n");
}

int iommu_group_get(const char *pci_addr)
{
    char buf[512], buf2[512], *ptr;
    int len, group;

    len = snprintf(buf, sizeof(buf) - 1, "/sys/bus/pci/devices/0000:%s/iommu_group",
                   pci_addr);
    buf[len] = 0;

    len = readlink(buf, buf2, sizeof(buf2));
    buf2[len] = 0;

    ptr = strrchr(buf2, '/');
    group = strtol(ptr + 1, NULL, 10);

    return group;
}

const char *vfio_region_names[] = {
    [VFIO_PCI_BAR0_REGION_INDEX] = "BAR0",
    [VFIO_PCI_BAR1_REGION_INDEX] = "BAR1",
    [VFIO_PCI_BAR2_REGION_INDEX] = "BAR2",
    [VFIO_PCI_BAR3_REGION_INDEX] = "BAR3",
    [VFIO_PCI_BAR4_REGION_INDEX] = "BAR4",
    [VFIO_PCI_BAR5_REGION_INDEX] = "BAR5",
    [VFIO_PCI_ROM_REGION_INDEX] = "ROM ",
    [VFIO_PCI_CONFIG_REGION_INDEX] = "CONF",
};

const char *vfio_irq_names[] = {
    [VFIO_PCI_INTX_IRQ_INDEX] = "INTX",
    [VFIO_PCI_MSI_IRQ_INDEX] = "MSI ",
    [VFIO_PCI_MSIX_IRQ_INDEX] = "MSIX",
    [VFIO_PCI_ERR_IRQ_INDEX] = "ERR ",
    [VFIO_PCI_REQ_IRQ_INDEX] = "REQ ",
};

int vfio_device_dump(int group, const char *pci_addr)
{
    int ret, device, i;
    char buf[512];
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };

    /* Get a file descriptor for the device */
    ret = snprintf(buf, sizeof(buf) - 1, "0000:%s", pci_addr);
    buf[ret] = 0;
    device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, buf);
    if (device == -1) {
        perror("ioctl() GET_DEVICE_FD failed");
        return -1;
    }

    /* Test and setup the device */
    ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
    if (ret) {
        perror("ioctl() GET_INFO failed");
        return -1;
    }

    printf("Device '%s' region count: %d\n", pci_addr,
           device_info.num_regions);
    for (i = 0; i < device_info.num_regions; i++) {
        struct vfio_region_info reg = { .argsz = sizeof(reg) };

        reg.index = i;

        ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &reg);

        /* Setup mappings... read/write offsets, mmaps
         * For PCI devices, config space is a region */
        printf("  Region %d name %s start 0x%lx flag %d size %lu\n",
               i, vfio_region_names[i], reg.offset, reg.flags, reg.size);
    }

    printf("Device '%s' irq count: %d\n", pci_addr,
           device_info.num_regions);
    for (i = 0; i < device_info.num_irqs; i++) {
        struct vfio_irq_info irq = { .argsz = sizeof(irq) };

        irq.index = i;

        ioctl(device, VFIO_DEVICE_GET_IRQ_INFO, &irq);

        /* Setup IRQs... eventfds, VFIO_DEVICE_SET_IRQS */
        printf("  IRQ %d name %s flags %d count %d\n",
               i, vfio_irq_names[i], irq.flags, irq.count);
    }

    /* Gratuitous device reset and go... */
    printf("Testing device '%s' RESET... ", pci_addr);
	ret = ioctl(device, VFIO_DEVICE_RESET);
    if (ret) {
        printf("error (but this is okay)\n", pci_addr);
    } else {
        printf("successfully\n", pci_addr);
    }

    return 0;
}

int vfio_container_set_iommu(int container)
{
    int ret;
    struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(iommu_info) };

    /* Enable the IOMMU model we want */
    ret = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    if (ret) {
        printf("ioctl() failed with SET_IOMMU");
        return -1;
    }

    /* Get addition IOMMU info */
    ret = ioctl(container, VFIO_IOMMU_GET_INFO, &iommu_info);
    if (ret) {
        printf("ioctl() failed with GET_INFO");
        return -1;
    }
}

/* Return group (>0), or <0 if failed. */
int vfio_container_bind_device(int container, const char *pci_addr)
{
    int group, ret;
    char buf[512];
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };

    if (!is_pci_addr(pci_addr)) {
        printf("Please input pci_addr like BB:DD.FF.\n\n");
        return -1;
    }

    group = iommu_group_get(pci_addr);
    printf("Device '%s' IOMMU group %d\n", pci_addr, group);

    /* Open the group */
    ret = snprintf(buf, sizeof(buf) - 1, "/dev/vfio/%d", group);
    buf[ret] = 0;
    group = open(buf, O_RDWR);
    if (group == -1) {
        perror("open group failed");
        return -1;
    }

    /* Test the group is viable and available */
    ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
    if (ret) {
        perror("ioctl() failed with GET_STATUS");
        return -1;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        /* Group is not viable (ie, not all devices bound for vfio) */
        printf("group is not viable\n");
        return -1;
    }

    /* Add the group to the container */
    ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
    if (ret) {
        printf("ioctl() failed with SET_CONTAINER");
        return -1;
    }

    return group;
}

int vfio_container_init(void)
{
    int container;

    /* Create a new container */
    container = open("/dev/vfio/vfio", O_RDWR);

    if (ioctl(container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        printf("unknown API version\n");
        return -1;
    }

    if (!ioctl(container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        /* Doesn't support the IOMMU driver we want. */
        printf("don't support VFIO IOMMU type1\n");
        return -1;
    }

    return container;
}

int vfio_container_do_map(int container)
{
    int ret;
    struct vfio_iommu_type1_dma_map dma_map = { .argsz = sizeof(dma_map) };

    /* Allocate some space and setup a DMA mapping */
#define DMA_SIZE (1 * 1024 * 1024)
    dma_map.vaddr = (uint64_t)mmap(0, DMA_SIZE, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (dma_map.vaddr == (uint64_t)MAP_FAILED) {
        perror("mmap() failed");
        return -1;
    } 
    dma_map.size = DMA_SIZE;
    dma_map.iova = 0; /* 1MB starting at 0x0 from device view */
    dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

    ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
    if (ret) {
        perror("ioctl() MAP_DMA failed");
        return -1;
    }

    return 0;
}

int main(int argc, const char *argv[])
{
    int container, group, ret, next;
    char buf[512];
    const char *pci_addr;

    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }

    container = vfio_container_init();
    if (container < 0) {
        return container;
    }

    pci_addr = argv[1];

    group = vfio_container_bind_device(container, pci_addr);
    if (group < 0) {
        return group;
    }

    if (vfio_container_set_iommu(container)) {
        return -1;
    }

    if (vfio_device_dump(group, pci_addr)) {
        return -1;
    }

    if (vfio_container_do_map(container)) {
        return -1;
    }

    /*
     * Then, bind all the rest of the devices into the same IOMMU
     * container.
     */
    next = 2;
    while (argv[next]) {
        printf("==================\n");
        pci_addr = argv[next++];
        printf("Found extra device '%s', binding to same group\n",
               pci_addr);
        group = vfio_container_bind_device(container, pci_addr);
        if (group < 0) {
            return -1;
        }
        if (vfio_device_dump(group, pci_addr)) {
            return -1;
        }
    }

    return 0;
}
