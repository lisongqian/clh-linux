#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#define DRV_NAME "ivshmem"
#define IVSHMEM_VENDOR_ID 0x1af4
#define IVSHMEM_DEVICE_ID 0x1110
#define IVSHMEM_REGISTER_BAR 0
#define IVSHMEM_MEMORY_BAR 2

struct ivshmem_device {
	struct pci_dev *pdev;
	void __iomem *regs;
	void __iomem *shmem;
	unsigned long shmem_size;
	struct cdev cdev;
	dev_t devno;
};

static struct ivshmem_device *iv_dev;

static int ivshmem_open(struct inode *inode, struct file *filp)
{
	filp->private_data = iv_dev;
	return 0;
}

static int ivshmem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ivshmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ivshmem_device *dev = filp->private_data;
	return remap_pfn_range(vma, vma->vm_start,
			       virt_to_phys(dev->shmem) >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static const struct file_operations ivshmem_fops = {
	.owner = THIS_MODULE,
	.open = ivshmem_open,
	.release = ivshmem_release,
	.mmap = ivshmem_mmap,
};

static int ivshmem_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	char buff[64] = { 0 };

	printk("ivshmem: Probing for ivshmem Device\n");
	ret = pci_enable_device(pdev);
	if (ret) {
		printk(KERN_ERR "Cannot probe ivshmem device %s: error %d\n",
		       pci_name(pdev), ret);
		return ret;
	}

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret < 0) {
		printk(KERN_ERR "ivshmem: cannot request regions\n");
		goto pci_disable;
	} else {
		printk(KERN_ERR "ivshmem: result is %d\n", ret);
	}

	iv_dev = kzalloc(sizeof(struct ivshmem_device), GFP_KERNEL);
	if (!iv_dev)
		return -ENOMEM;
	pci_set_drvdata(pdev, iv_dev);
	iv_dev->pdev = pdev;

	iv_dev->shmem_size = pci_resource_len(pdev, IVSHMEM_MEMORY_BAR);

	iv_dev->regs = pci_iomap(pdev, IVSHMEM_REGISTER_BAR, 0);
	iv_dev->shmem = pci_iomap(pdev, IVSHMEM_MEMORY_BAR, 0);
	printk(KERN_INFO "ivshmem: iomap base = 0x%lu \n",
	       (unsigned long)iv_dev->shmem);
	if (!iv_dev->shmem) {
		printk(KERN_ERR "ivshmem: cannot iomap region of size %ld\n",
		       iv_dev->shmem_size);
		goto pci_release;
	}

	sprintf(buff, "Hello host, I am guest!");
	memcpy(iv_dev->shmem, buff, sizeof(buff));

	ret = alloc_chrdev_region(&iv_dev->devno, 0, 1, DRV_NAME);
	if (ret) {
		printk(KERN_ERR "ivshmem: cannot alloc char dev of devno %d\n",
		       iv_dev->devno);
		goto cdev_unregister;
	}
	cdev_init(&iv_dev->cdev, &ivshmem_fops);
	ret = cdev_add(&iv_dev->cdev, iv_dev->devno, 1);
	if (ret) {
		printk(KERN_ERR "ivshmem: cannot add char dev\n");
		goto cdev_delete;
	}

	return 0;

cdev_delete:
	cdev_del(&iv_dev->cdev);
cdev_unregister:
	unregister_chrdev_region(iv_dev->devno, 1);
pci_release:
	pci_iounmap(pdev, iv_dev->regs);
	pci_iounmap(pdev, iv_dev->shmem);
	kfree(iv_dev);
pci_disable:
	pci_disable_device(pdev);
	return ret;
}

static void ivshmem_remove(struct pci_dev *pdev)
{
	struct ivshmem_device *dev = pci_get_drvdata(pdev);

	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->devno, 1);
	pci_iounmap(pdev, dev->regs);
	pci_iounmap(pdev, dev->shmem);
	kfree(dev);
}

static struct pci_device_id ivshmem_ids[] = { { PCI_DEVICE(IVSHMEM_VENDOR_ID,
							   IVSHMEM_DEVICE_ID) },
					      { 0 } };

static struct pci_driver ivshmem_pci_driver = {
	.name = DRV_NAME,
	.id_table = ivshmem_ids,
	.probe = ivshmem_probe,
	.remove = ivshmem_remove,
};

module_pci_driver(ivshmem_pci_driver);

MODULE_AUTHOR("Songqian Li <sionli@tencent.com>");
MODULE_DESCRIPTION("Inter-VM shared memory device driver");
MODULE_LICENSE("GPL");