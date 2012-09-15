/*
 *  smo8800.c - Dell Latitude ACPI SMO8800 accelerometer driver
 *
 *  Copyright (C) 2012 Sonal Santan < sonal DOT santan AT gmail DOT com >
 *  
 *  DELL Latitude laptops come with a The HardDisk Active Protection System (hdaps) is present in IBM ThinkPads
 *  starting with the R40, T41, and X40.  It provides a basic two-axis
 *  accelerometer and other data, such as the device's temperature.
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
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/interrupt.h>

#define DRIVER_NAME     "DELL ACPI SMO8800 Driver"

static int smo8800_add(struct acpi_device *device);
static int smo8800_remove(struct acpi_device *device, int type);
static int smo8800_suspend(struct acpi_device *device, pm_message_t state);
static int smo8800_resume(struct acpi_device *device);

struct smo8800 {
        void		*bus_priv; 
        u32		irq;   
        atomic_t	count;     /* interrupt count after last read */
};


struct smo8800 smo8800_dev;

static const struct acpi_device_id acc_device_ids[] = {
        { "SMO8800", 0},
        { "", 0},
};

MODULE_DEVICE_TABLE(acpi, acc_device_ids);

static struct acpi_driver latitude_acc_driver = {
        .name =         DRIVER_NAME,
        .class =        "Latitude",
        .ids =          acc_device_ids,
        .ops =          {
                .add =          smo8800_add,
                .remove =       smo8800_remove,
                .suspend =      smo8800_suspend,
                .resume  =      smo8800_resume,
        },
        .owner =	THIS_MODULE,
};

static irqreturn_t smo8800_interrupt_quick(int irq, void *data)
{
	struct smo8800 *smo = data;
	atomic_inc(&smo->count);
	return IRQ_WAKE_THREAD;
}


static irqreturn_t smo8800_interrupt_thread(int irq, void *data)
{
	struct smo8800 *smo = data;
        printk(KERN_DEBUG DRIVER_NAME ": Interrupt count %d\n", smo->count);
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

        return 0;
}

static int smo8800_remove(struct acpi_device *device, int type)
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
}

module_init(smo8800_init);
module_exit(smo8800_exit);

MODULE_DESCRIPTION(DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sonal Santan");
