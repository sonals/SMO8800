/*
 *  smo8800.c - Dell Latitude ACPI SMO8800 freefall sensor driver
 *
 *  Copyright (C) 2012 Sonal Santan < sonal DOT santan AT gmail DOT com >
 *
 *  This is loosely based on lis3lv02d driver.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>

#define DRIVER_NAME	"smo8800"

static int smo8800_add(struct acpi_device *device);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
static int smo8800_remove(struct acpi_device *device);
#else
static int smo8800_remove(struct acpi_device *device, int type);
#endif
static int smo8800_suspend(struct acpi_device *device, pm_message_t state);
static int smo8800_resume(struct acpi_device *device);
static int smo8800_misc_open(struct inode *inode, struct file *file);
static int smo8800_misc_release(struct inode *inode, struct file *file);
static ssize_t smo8800_misc_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos);

struct smo8800 {
	void			*bus_priv;
	u32			irq;
	atomic_t		count;	     /* interrupt count after last read */
	struct miscdevice	miscdev;     /* for /dev/freefall */
	unsigned long		misc_opened; /* whether the device is open */
	wait_queue_head_t	misc_wait;   /* Wait queue for the misc device */
};


struct smo8800 smo8800_dev;

static const struct acpi_device_id acc_device_ids[] = {
	{ "SMO8800", 0},
	{ "", 0},
};

MODULE_DEVICE_TABLE(acpi, acc_device_ids);

static struct acpi_driver latitude_acc_driver = {
	.name =		DRIVER_NAME,
	.class =	"Latitude",
	.ids =		acc_device_ids,
	.ops =		{
		.add =		smo8800_add,
		.remove =	smo8800_remove,
/*
		.suspend =	smo8800_suspend,
		.resume	 =	smo8800_resume,
*/
	},
	.owner =	THIS_MODULE,
};


static const struct file_operations smo8800_misc_fops = {
	.owner	 = THIS_MODULE,
	.read	 = smo8800_misc_read,
	.open	 = smo8800_misc_open,
	.release = smo8800_misc_release,
};


static int smo8800_misc_open(struct inode *inode, struct file *file)
{
	struct smo8800 *smo_data = container_of(file->private_data,
						struct smo8800, miscdev);

	if (test_and_set_bit(0, &smo_data->misc_opened))
		return -EBUSY; /* already open */

	atomic_set(&smo_data->count, 0);
	return 0;
}

static int smo8800_misc_release(struct inode *inode, struct file *file)
{
	struct smo8800 *smo_data = container_of(file->private_data,
						struct smo8800, miscdev);

	clear_bit(0, &smo_data->misc_opened); /* release the device */
	return 0;
}

static ssize_t smo8800_misc_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos)
{
	struct smo8800 *smo_data = container_of(file->private_data,
						struct smo8800, miscdev);

	DECLARE_WAITQUEUE(wait, current);
	u32 data;
	unsigned char byte_data;
	ssize_t retval = 1;

	if (count < 1)
		return -EINVAL;

	add_wait_queue(&smo_data->misc_wait, &wait);
	while (true) {
		set_current_state(TASK_INTERRUPTIBLE);
//		printk(KERN_DEBUG DRIVER_NAME ": COUNT %d\n", smo_data->count);
		data = atomic_xchg(&smo_data->count, 0);
//		printk(KERN_DEBUG DRIVER_NAME ": COUNT %d DATA %d\n", smo_data->count, data);
		if (data) {
			break;
		}

		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}

		schedule();
	}

	if (data < 255)
		byte_data = data;
	else
		byte_data = 255;

	/* make sure we are not going into copy_to_user() with
	 * TASK_INTERRUPTIBLE state */
	set_current_state(TASK_RUNNING);
	if (copy_to_user(buf, &byte_data, sizeof(byte_data)))
		retval = -EFAULT;

out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&smo_data->misc_wait, &wait);

	return retval;
}

static irqreturn_t smo8800_interrupt_quick(int irq, void *data)
{
	struct smo8800 *smo = data;
	atomic_inc(&smo->count);
	wake_up_interruptible(&smo->misc_wait);
	return IRQ_WAKE_THREAD;
}


static irqreturn_t smo8800_interrupt_thread(int irq, void *data)
{
	printk(KERN_DEBUG DRIVER_NAME ": Detected free fall\n");
	return IRQ_HANDLED;
}

static acpi_status smo8800_get_resource(struct acpi_resource *resource, void *context)
{
	if (resource->type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		struct acpi_resource_extended_irq *irq;
		u32 *device_irq = context;

		irq = &resource->data.extended_irq;
		*device_irq = irq->interrupts[0];
	}

	return AE_OK;
}

static void smo8800_enum_resources(struct acpi_device *device)
{
	acpi_status status;

	status = acpi_walk_resources(device->handle, METHOD_NAME__CRS,
				     smo8800_get_resource, &smo8800_dev.irq);
	if (ACPI_FAILURE(status))
		printk(KERN_DEBUG DRIVER_NAME ": Error getting resources\n");
}

static int smo8800_add(struct acpi_device *device)
{
	int err;
	if (!device)
		return -EINVAL;
	smo8800_dev.bus_priv = device;
	atomic_set(&smo8800_dev.count, 0);
	device->driver_data = &smo8800_dev;
	err = 0;

	smo8800_enum_resources(device);

	if (!smo8800_dev.irq) {
		printk(KERN_DEBUG DRIVER_NAME "No IRQ. Disabling /dev/freefall\n");
		goto out;
	}

	printk(KERN_DEBUG DRIVER_NAME ": IRQ %d\n", smo8800_dev.irq);

	err = request_threaded_irq(smo8800_dev.irq, smo8800_interrupt_quick,
				   smo8800_interrupt_thread,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   DRIVER_NAME, &smo8800_dev);

out:
	return err;
}

static int __init smo8800_init(void)
{
	int result = 0;

	result = acpi_bus_register_driver(&latitude_acc_driver);
	if (result < 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error registering driver\n"));
		return -ENODEV;
	}

	smo8800_dev.miscdev.minor	= MISC_DYNAMIC_MINOR;
	smo8800_dev.miscdev.name	= "freefall";
	smo8800_dev.miscdev.fops	= &smo8800_misc_fops;
	init_waitqueue_head(&smo8800_dev.misc_wait);
	if (misc_register(&smo8800_dev.miscdev))
		pr_err("misc_register failed\n");

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
static int smo8800_remove(struct acpi_device *device)
#else
static int smo8800_remove(struct acpi_device *device, int type)
#endif
{
	if (smo8800_dev.irq)
		free_irq(smo8800_dev.irq, &smo8800_dev);
	return 0;
}

#ifdef CONFIG_PM
static int smo8800_suspend(struct acpi_device *device, pm_message_t state)
{
	return 0;
}

static int smo8800_resume(struct acpi_device *device)
{
	return 0;
}
#else
#define smo8800_suspend NULL
#define smo8800_resume NULL
#endif

static void __exit smo8800_exit(void)
{
	acpi_bus_unregister_driver(&latitude_acc_driver);
	misc_deregister(&smo8800_dev.miscdev);
}

module_init(smo8800_init);
module_exit(smo8800_exit);

MODULE_DESCRIPTION("Dell laptop freefall driver (ACPI " DRIVER_NAME ")");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sonal Santan");
