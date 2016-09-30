#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Xu <peterx@redhat.com>");
MODULE_DESCRIPTION("Driver for QEMU's edu device");

#define PCI_VENDOR_ID_QEMU          (0x1234)
#define PCI_DEVICE_ID_EDU           (0x11e8)

/* The only bar used by EDU device */
#define EDU_BAR_MEM                 (0)
#define EDU_MAGIC                   (0xed)
#define EDU_VERSION                 (0x100)
#define EDU_DMA_BUF_SIZE            (1 << 20)
#define EDU_INPUT_BUF_SIZE          (256)

#define EDU_REG_ID                  (0x0)
#define EDU_REG_ALIVE               (0x4)
#define EDU_REG_FACTORIAL           (0x8)
#define EDU_REG_STATUS              (0x20)

#define EDU_STATUS_FACTORIAL        (0x1)
#define EDU_STATUS_INT_ENABLE       (0x80)

static const char edu_driver_name[] = "edu";
static struct dentry *edu_dbg_root;
static char edu_input[EDU_INPUT_BUF_SIZE];
static atomic_t edu_device_count;

struct edu_dev {
	int bars;
	u16 version;
	u8 __iomem *mem_addr;
	char edu_buf[EDU_DMA_BUF_SIZE];
	struct pci_dev *pci_dev;

	/* Factorial */
	struct dentry *file_factorial;
	u32 factorial_result;
};

static const struct pci_device_id edu_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_QEMU, PCI_DEVICE_ID_EDU), 0},
	{0, }
};
MODULE_DEVICE_TABLE(pci, edu_pci_tbl);

static u32 edu_reg_read(struct edu_dev *dev, u32 addr)
{
	return *(volatile u32 *)(dev->mem_addr + addr);
}

static void edu_reg_write(struct edu_dev *dev, u32 addr, u32 value)
{
	*(volatile u32 *)(dev->mem_addr + addr) = value;
}

static int edu_check_alive(struct edu_dev *dev)
{
	static u32 live_count = 1;
	u32 value;

	edu_reg_write(dev, EDU_REG_ALIVE, live_count++);
	value = edu_reg_read(dev, EDU_REG_ALIVE);
	if (live_count - 1 == ~value) {
		pr_info("edu: alive check passed\n");
		return 0;
	} else {
		pr_info("edu: alive check failed\n");
		return -1;
	}
}

static u32 edu_status(struct edu_dev *dev)
{
	return edu_reg_read(dev, EDU_REG_STATUS);
}

static bool edu_factorial_busy(struct edu_dev *dev)
{
	return edu_status(dev) & EDU_STATUS_FACTORIAL;
}

static ssize_t edu_factorial_ops_read(struct file *filp,
				      char __user *buffer,
				      size_t count, loff_t *ppos)
{
	struct edu_dev *dev = filp->private_data;
	int len;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;

	snprintf(edu_input, sizeof(edu_input), "%u\n",
		 dev->factorial_result);
	edu_input[sizeof(edu_input) - 1] = '\0';
	len = simple_read_from_buffer(buffer, count, ppos,
				      edu_input, strlen(edu_input));

	return len;
}

static void edu_factorial_wait_finish(struct edu_dev *dev)
{
	while (edu_factorial_busy(dev));
}

static ssize_t edu_factorial_ops_write(struct file *filp,
				       const char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct edu_dev *dev = filp->private_data;
	int len;
	u32 value;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	if (count >= sizeof(edu_input))
		return -ENOSPC;

	len = simple_write_to_buffer(edu_input, sizeof(edu_input) - 1,
				     ppos, buffer, count);
	if (len < 0)
		return len;

	edu_input[len] = '\0';

	if (kstrtou32(edu_input, 10, &value)) {
		pr_warn("edu: factorial: invalid input: '%s'\n", edu_input);
		/* Throw buf */
		return count;
	}

	edu_reg_write(dev, EDU_REG_FACTORIAL, value);
	edu_factorial_wait_finish(dev);
	dev->factorial_result = edu_reg_read(dev, EDU_REG_FACTORIAL);

	return count;
}

static const struct file_operations edu_factorial_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = edu_factorial_ops_read,
	.write = edu_factorial_ops_write,
};

static int edu_debugfs_create(struct edu_dev *dev)
{
	struct dentry *pfile;

	pfile = debugfs_create_file("factorial", 0600, edu_dbg_root, dev,
				    &edu_factorial_fops);
	if (!pfile) {
		pr_err("edu: failed to create debug files\n");
		return -1;
	}
	dev->file_factorial = pfile;

	return 0;
}

static void edu_debugfs_cleanup(struct edu_dev *dev)
{
	if (dev->file_factorial) {
		debugfs_remove(dev->file_factorial);
		dev->file_factorial = NULL;
	}
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int bars;
	int ret;
	struct edu_dev *edu_dev;
	u32 value;

	pr_info("edu: detected device\n");

	if (atomic_cmpxchg(&edu_device_count, 0, 1) == 1) {
		/*
		 * We have one device already, disable further edu
		 * devices
		 */
		pr_warn("edu: already have edu device, skip init\n");
		return -1;
	}

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (bars != 1 << EDU_BAR_MEM) {
		pr_err("edu: detected other bars than %d\n", EDU_BAR_MEM);
		return -1;
	}

	ret = pci_enable_device_mem(pdev);
	if (ret) {
		pr_err("edu: enable device mem failed %d\n", ret);
		return ret;
	}

	ret = pci_request_selected_regions(pdev, bars, edu_driver_name);
	if (ret) {
		pr_err("edu: request mem region failed %d\n", ret);
		goto request_err;
	}

	pci_set_master(pdev);

	edu_dev = kzalloc(sizeof(*edu_dev), GFP_KERNEL);
	edu_dev->bars = bars;
	pci_set_drvdata(pdev, edu_dev);
	edu_dev->mem_addr = pci_ioremap_bar(pdev, EDU_BAR_MEM);
	edu_dev->pci_dev = pdev;

	value = edu_reg_read(edu_dev, EDU_REG_ID);
	if ((value & 0xff) != EDU_MAGIC) {
		pr_err("edu: magic (0x%02x) mismatch with 0x%02x\n",
		       value & 0xff, EDU_MAGIC);
		ret = -1;
		goto magic_fail;
	}

	edu_dev->version = (u16)(value >> 16);
	if (edu_dev->version != EDU_VERSION) {
		pr_err("edu: version (0x%02x) mismatch with 0x%02x\n",
		       edu_dev->version, EDU_VERSION);
		ret = -1;
		goto magic_fail;
	}

	if (edu_check_alive(edu_dev)) {
		pr_err("edu: device alive check failed\n");
		ret = -1;
		goto magic_fail;
	}

	if (edu_debugfs_create(edu_dev)) {
		ret = -1;
		goto magic_fail;
	}

	pr_info("edu: device initialized successfully (ver=0x%02x)\n",
		edu_dev->version);
	return 0;

 magic_fail:
	iounmap(edu_dev->mem_addr);
	pci_release_selected_regions(pdev, edu_dev->bars);
	kfree(edu_dev);
 request_err:
	pci_disable_device(pdev);
	return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
	struct edu_dev *edu_dev = pci_get_drvdata(pdev);

	edu_debugfs_cleanup(edu_dev);

	iounmap(edu_dev->mem_addr);
	pci_release_selected_regions(pdev, edu_dev->bars);
	pci_disable_device(pdev);

	kfree(edu_dev);

	atomic_dec(&edu_device_count);

	pr_info("edu: releasd device\n");
	return;
}

static struct pci_driver edu_driver = {
	.name     = edu_driver_name,
	.id_table = edu_pci_tbl,
	.probe    = edu_probe,
	.remove   = edu_remove,
};

static int __init edu_init(void)
{
	int ret;

	/*
	 * CAUTION: this needs to be before pci_register_driver(),
	 * because if we first insert the device, then load this edu
	 * driver, the device will be probed during
	 * pci_register_driver(), and that will leads to an
	 * uninitialized edu_dbg_root.
	 */
	edu_dbg_root = debugfs_create_dir(edu_driver_name, NULL);
	if (!edu_dbg_root) {
		pr_err("edu: init debugfs failed\n");
		return -1;
	}

	ret = pci_register_driver(&edu_driver);
	if (ret) {
		pr_err("edu: failed to load device driver\n");
		return ret;
	}

	pr_info("edu: device driver loaded\n");
	return 0;
}

static void __exit edu_cleanup(void)
{
	debugfs_remove_recursive(edu_dbg_root);
	pci_unregister_driver(&edu_driver);
	pr_info("edu: device driver unloaded\n");
}

module_init(edu_init);
module_exit(edu_cleanup);
