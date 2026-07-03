// SPDX-License-Identifier: GPL-2.0
/*
 * pixxel_platform_driver.c
 *
 * Platform driver for the pixxel,virt-dev virtual device.
 *
 * The device has two simulated registers (no real hardware backing):
 *   Enable  (W) @ base+0x00 — write 1 to enable, 0 to disable
 *   Status  (R) @ base+0x04 — mirrors Enable ~50 ms after a write
 *
 * User-space interface: /dev/pixxel (character device)
 *   write "1\n" or "0\n" → sets Enable register
 *   read               → returns current Status register value ("1\n" or "0\n")
 *
 * The 50 ms delay is implemented with a kernel delayed_work item.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>

#define DRIVER_NAME       "pixxel_platform_driver"
#define STATUS_DELAY_MS   50

/* Register offsets (virtual — backed by pixxel_regs below, not ioremap accesses) */
#define REG_ENABLE        0x00
#define REG_STATUS        0x04

struct pixxel_regs {
	u32 enable;   /* mirrors write to Enable register */
	u32 status;   /* updated STATUS_DELAY_MS after enable changes */
};

struct pixxel_dev {
	void __iomem          *base;          /* ioremap base — claimed but not accessed */
	struct pixxel_regs     regs;          /* software-simulated register state */
	spinlock_t             lock;
	struct delayed_work    status_work;   /* fires STATUS_DELAY_MS after Enable write */
	struct cdev            cdev;
	dev_t                  devno;
	struct class          *cls;
};

/* ---------- delayed work: propagate enable → status after 50 ms ---------- */

static void pixxel_status_update(struct work_struct *work)
{
	struct pixxel_dev *dev =
		container_of(to_delayed_work(work), struct pixxel_dev, status_work);
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->regs.status = dev->regs.enable;
	spin_unlock_irqrestore(&dev->lock, flags);

	pr_info(DRIVER_NAME ": status register updated → %u\n", dev->regs.status);
}

/* ---------- character device file operations ---------- */

static int pixxel_open(struct inode *inode, struct file *filp)
{
	filp->private_data =
		container_of(inode->i_cdev, struct pixxel_dev, cdev);
	return 0;
}

/*
 * write: accept "0" or "1" (with optional newline / whitespace) to set Enable.
 * Cancels any in-flight status update and reschedules the 50 ms timer.
 */
static ssize_t pixxel_write(struct file *filp, const char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct pixxel_dev *dev = filp->private_data;
	char kbuf[4] = {0};
	u32 val;
	unsigned long flags;

	if (count == 0)
		return 0;

	if (copy_from_user(kbuf, ubuf, min(count, sizeof(kbuf) - 1)))
		return -EFAULT;

	if (kstrtou32(skip_spaces(kbuf), 10, &val) || val > 1)
		return -EINVAL;

	spin_lock_irqsave(&dev->lock, flags);
	dev->regs.enable = val;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* restart the 50 ms window every time Enable is written */
	cancel_delayed_work(&dev->status_work);
	schedule_delayed_work(&dev->status_work, msecs_to_jiffies(STATUS_DELAY_MS));

	pr_info(DRIVER_NAME ": enable register ← %u (status update in %d ms)\n",
		val, STATUS_DELAY_MS);
	return count;
}

/* read: return the current Status register as an ASCII "0\n" or "1\n" */
static ssize_t pixxel_read(struct file *filp, char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	struct pixxel_dev *dev = filp->private_data;
	unsigned long flags;
	char kbuf[4];
	int len;
	u32 status;

	/* only serve one read per open position */
	if (*ppos > 0)
		return 0;

	spin_lock_irqsave(&dev->lock, flags);
	status = dev->regs.status;
	spin_unlock_irqrestore(&dev->lock, flags);

	len = scnprintf(kbuf, sizeof(kbuf), "%u\n", status);
	if (copy_to_user(ubuf, kbuf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static const struct file_operations pixxel_fops = {
	.owner = THIS_MODULE,
	.open  = pixxel_open,
	.read  = pixxel_read,
	.write = pixxel_write,
};

/* ---------- platform driver probe / remove ---------- */

static int pixxel_probe(struct platform_device *pdev)
{
	struct pixxel_dev *dev;
	struct resource *res;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);
	INIT_DELAYED_WORK(&dev->status_work, pixxel_status_update);

	/* claim the register region described in the device tree */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no MEM resource in device tree\n");
		return -ENODEV;
	}

	/*
	 * ioremap the region to satisfy the platform driver contract.
	 * All register state is maintained in pixxel_regs (software simulation);
	 * the ioremap'd pointer is stored but never dereferenced — there is no
	 * physical hardware at this address in the QEMU virt machine.
	 */
	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	/* allocate a character device number */
	ret = alloc_chrdev_region(&dev->devno, 0, 1, DRIVER_NAME);
	if (ret) {
		dev_err(&pdev->dev, "alloc_chrdev_region failed: %d\n", ret);
		return ret;
	}

	cdev_init(&dev->cdev, &pixxel_fops);
	ret = cdev_add(&dev->cdev, dev->devno, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed: %d\n", ret);
		goto err_unreg;
	}

	/* class_create single-arg form (Linux ≥ 6.4) */
	dev->cls = class_create(DRIVER_NAME);
	if (IS_ERR(dev->cls)) {
		ret = PTR_ERR(dev->cls);
		goto err_cdev;
	}

	if (!device_create(dev->cls, NULL, dev->devno, NULL, "pixxel")) {
		ret = -ENOMEM;
		goto err_class;
	}

	platform_set_drvdata(pdev, dev);

	dev_info(&pdev->dev,
		 "probed — registers at 0x%llx, /dev/pixxel created\n",
		 (unsigned long long)res->start);
	return 0;

err_class:
	class_destroy(dev->cls);
err_cdev:
	cdev_del(&dev->cdev);
err_unreg:
	unregister_chrdev_region(dev->devno, 1);
	return ret;
}

static void pixxel_remove_common(struct platform_device *pdev)
{
	struct pixxel_dev *dev = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&dev->status_work);
	device_destroy(dev->cls, dev->devno);
	class_destroy(dev->cls);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->devno, 1);

	dev_info(&pdev->dev, "removed\n");
}

/*
 * struct platform_driver.remove changed from int(*)() to void(*)() in
 * Linux 6.11 (the old int-returning form was removed outright, not
 * deprecated). The host kernel here is 6.17, but Yocto's qemuarm64 BSP
 * pins linux-yocto to 6.6 — both must build from this one source file.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void pixxel_remove(struct platform_device *pdev)
{
	pixxel_remove_common(pdev);
}
#else
static int pixxel_remove(struct platform_device *pdev)
{
	pixxel_remove_common(pdev);
	return 0;
}
#endif

static const struct of_device_id pixxel_of_match[] = {
	{ .compatible = "pixxel,virt-dev" },
	{ }
};
MODULE_DEVICE_TABLE(of, pixxel_of_match);

static struct platform_driver pixxel_driver = {
	.probe  = pixxel_probe,
	.remove = pixxel_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.of_match_table = pixxel_of_match,
	},
};

module_platform_driver(pixxel_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pixxel Assignment");
MODULE_DESCRIPTION("Virtual platform device driver for pixxel,virt-dev");
MODULE_VERSION("1.0");
