#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/ioctl.h>
#include <linux/vfio.h>
#include <linux/pci_regs.h>
#include <linux/mman.h>

void usage(char *name)
{
    fprintf(stderr, "usage: %s <ssss:bb:dd.f>\n", name);
    fprintf(stderr, "\tssss: PCI segment, ex. 0000\n");
    fprintf(stderr, "\tbb:   PCI bus, ex. 01\n");
    fprintf(stderr, "\tdd:   PCI device, ex. 06\n");
    fprintf(stderr, "\tf:    PCI function, ex. 0\n");
}

#define  KB  (1UL << 10)
#define  MB  (1UL << 20)

static int enable_mem(int device, struct vfio_region_info *config_info)
{
    unsigned short cmd;

    if (pread(device, &cmd, 2, config_info->offset + PCI_COMMAND) != 2) {
        fprintf(stderr, "Failed to read command register\n");
        return -errno;
    }

    if (!(cmd & PCI_COMMAND_MEMORY)) {
        cmd |= PCI_COMMAND_MEMORY;

        if (pwrite(device, &cmd, 2, config_info->offset + PCI_COMMAND) != 2) {
            fprintf(stderr, "Failed to write command register\n");
            return -errno;
        }
    }

    printf("Memory ENABLED\n");

    return 0;
}

static double difftimespec(struct timespec start, struct timespec end)
{
    double a, b;

    a = start.tv_sec * 1000000000 + start.tv_nsec;
    b = end.tv_sec * 1000000000 + end.tv_nsec;

    return (b - a) / 1000000000;
}

static void read_bar(void *map, ssize_t size, ssize_t chunk)
{
    void *ptr = map, *end = map + size;
    struct timespec ts[2];
    unsigned long reads = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts[0]);

    for (;ptr < end; ptr += chunk, reads++)
        (void)*(volatile unsigned int *)ptr;

    clock_gettime(CLOCK_MONOTONIC, &ts[1]);
    printf("read(%ld) - %lfs\n", reads, difftimespec(ts[0], ts[1]));
}

static void *mmap_bar(int device, unsigned long offset, unsigned long size)
{
    struct timespec ts[2];
    void *map;

    clock_gettime(CLOCK_MONOTONIC, &ts[0]);
    /* Use !MAP_FIXED */
    map = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_SHARED, device, offset);
    clock_gettime(CLOCK_MONOTONIC, &ts[1]);
    printf("mmap()=%p - %lfs\n", map, difftimespec(ts[0], ts[1]));

    return map;
}

static void test_bar_with_range(int device, unsigned long offset, unsigned long size)
{
    unsigned long pgsz = getpagesize();
    void *map;

    printf("mmap BAR with memory ENABLED and read (offset=0x%lx, size=0x%lx)\n",
           offset, size);

    map = mmap_bar(device, offset, size);
    assert(map != MAP_FAILED);
    read_bar(map, size, pgsz);
    munmap(map, size);
}

int main(int argc, char **argv)
{
    int seg, bus, slot, func;
    int ret, container, group, device, groupid, *efdp;
    char path[50], iommu_group_path[50], *group_name;
    struct stat st;
    ssize_t len, pgsz;
    unsigned long i;
    unsigned int bar, val;
    struct vfio_group_status group_status = {
        .argsz = sizeof(group_status)
    };
    struct vfio_region_info region_info = {
        .argsz = sizeof(region_info)
    };
    struct vfio_region_info config_info = {
        .argsz = sizeof(config_info)
    };
    struct vfio_irq_info irq_info = {
        .argsz = sizeof(irq_info),
        .index = VFIO_PCI_MSIX_IRQ_INDEX
    };
    struct vfio_irq_set *irq_set;
    struct vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(dma_map),
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
    };

    if (argc != 2) {
        usage(argv[0]);
        return -1;
    }

    ret = sscanf(argv[1], "%04x:%02x:%02x.%d", &seg, &bus, &slot, &func);
    if (ret != 4) {
        fprintf(stderr, "Invalid device\n");
        usage(argv[0]);
        return -1;
    }

    /* Boilerplate vfio setup */
    container = open("/dev/vfio/vfio", O_RDWR);
    if (container < 0) {
        fprintf(stderr, "Failed to open /dev/vfio/vfio, %d (%s)\n",
                container, strerror(errno));
        return container;
    }

    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/",
             seg, bus, slot, func);

    ret = stat(path, &st);
    if (ret < 0) {
        fprintf(stderr, "No such device\n");
        return ret;
    }

    strncat(path, "iommu_group", sizeof(path) - strlen(path) - 1);

    len = readlink(path, iommu_group_path, sizeof(iommu_group_path));
    if (len <= 0) {
        fprintf(stderr, "No iommu_group for device\n");
        return -1;
    }

    iommu_group_path[len] = 0;
    group_name = basename(iommu_group_path);

    if (sscanf(group_name, "%d", &groupid) != 1) {
        fprintf(stderr, "Unknown group\n");
        return -1;
    }

    snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);
    group = open(path, O_RDWR);
    if (group < 0) {
        fprintf(stderr, "Failed to open %s, %d (%s)\n",
                path, group, strerror(errno));
        return group;
    }

    ret = ioctl(group, VFIO_GROUP_GET_STATUS, &group_status);
    if (ret) {
        fprintf(stderr, "ioctl(VFIO_GROUP_GET_STATUS) failed\n");
        return ret;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr,
                "Group not viable, all devices attached to vfio?\n");
        return -1;
    }

    ret = ioctl(group, VFIO_GROUP_SET_CONTAINER, &container);
    if (ret) {
        fprintf(stderr, "Failed to set group container\n");
        return ret;
    }

    ret = ioctl(container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
    if (ret) {
        fprintf(stderr, "Failed to set IOMMU\n");
        return ret;
    }

    snprintf(path, sizeof(path), "%04x:%02x:%02x.%d", seg, bus, slot, func);

    device = ioctl(group, VFIO_GROUP_GET_DEVICE_FD, path);
    if (device < 0) {
        fprintf(stderr, "Failed to get device\n");
        return -ENODEV;
    }

    config_info.index = VFIO_PCI_CONFIG_REGION_INDEX;
    ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &config_info);
    if (ret) {
        fprintf(stderr, "Failed to get config space region info\n");
        return ret;
    }

    printf("Config space starts at 0x%016lx\n", config_info.offset);

    for (i = 0; i < 6; i++) {
        if (pread(device, &bar, sizeof(bar),
                  config_info.offset + PCI_BASE_ADDRESS_0 + (4 * i)) !=
            sizeof(bar)) {
            fprintf(stderr, "Error reading BAR%d\n", i);
            return -errno;
        }

        printf("BAR%d: 0x%x\n", i, bar);
        if (!(bar & PCI_BASE_ADDRESS_SPACE)) {
            printf("Found memory BAR%d\n", i);
            break;

        tryagain:
            if (bar & PCI_BASE_ADDRESS_MEM_TYPE_64)
                i++;
        }
    }

    if (i >= 6) {
        fprintf(stderr, "No memory BARs found\n");
        return -ENODEV;
    }

    region_info.index = VFIO_PCI_BAR0_REGION_INDEX + i;
    ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info);
    if (ret) {
        fprintf(stderr, "Failed to get BAR%d region info\n", i);
        return ret;
    }
  
    if (!(region_info.flags & VFIO_REGION_INFO_FLAG_MMAP)) {
        printf("No mmap support, try next\n");
        goto tryagain;
    }

    pgsz = getpagesize();

    if (region_info.size < pgsz) {
        printf("Too small for mmap, try next\n");
        goto tryagain;
    }

    printf("BAR%d size: %ldMB\n", i, region_info.size >> 20);

    enable_mem(device, &config_info);

    /* Full bar */
    test_bar_with_range(device,
                        region_info.offset,
                        region_info.size);

    /* Chop head, tail, or both */
    test_bar_with_range(device,
                        region_info.offset + pgsz,
                        region_info.size - pgsz);
    test_bar_with_range(device,
                        region_info.offset,
                        region_info.size - pgsz);
    test_bar_with_range(device,
                        region_info.offset + pgsz,
                        region_info.size - 2 * pgsz);

    return 0;
}
