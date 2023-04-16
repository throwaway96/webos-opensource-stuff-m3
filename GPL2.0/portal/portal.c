/** @file 		portal.c
 *
 *  Portal of Power's Driver for Skylander's Games
 *
 *  @author     Cory McWilliams <cmcwilliams@vvisions.com>
 *  @version    1.0
 *  @date       2013.05.21
 *  @note       Additional information.
 *  @see
 */

//#define DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <asm/uaccess.h> 		/* copy_to_user */

#define DRIVER_AUTHOR "Cory McWilliams <cmcwilliams@vvisions.com>"
#define DRIVER_DESC "Activision Portal of Power Driver"

#define DEVICE_NAME "Portal of Power"
#define DRIVER_NAME "portal"

#define REQUEST_TYPE_HID 0x21

#define PORTAL_MAJOR 240

#define MAX_PORTAL_MSG 32

static DEFINE_MUTEX(portal_mutex);

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1430, 0x0150) },
	{ USB_DEVICE(0x1430, 0x0967) },
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

struct usb_portal {
	struct usb_device *udev;
};

static struct usb_portal* portal_dev;

static ssize_t portal_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
	int count;
	char readBuffer[MAX_PORTAL_MSG];
	int retval;

	mutex_lock(&portal_mutex);

	if (!portal_dev) {
		retval = -ENOENT;
		goto error;
	}

	retval = usb_interrupt_msg(portal_dev->udev, usb_rcvctrlpipe(portal_dev->udev, 1), readBuffer, min(length, sizeof(readBuffer)), &count, HZ / 30);

	if (retval < 0)
		dev_dbg(&portal_dev->udev->dev, "usb_control_msg for portal_read with length=%d returned %d and count=%d\n", length, retval, count);

	if (retval == 0) {
		retval = copy_to_user(buffer, readBuffer, count);
		if (retval == 0)
			retval = count;
		else {
			dev_dbg(&portal_dev->udev->dev, "copy_to_user could not copy %d bytes in portal_read\n", retval);
			retval = -EFAULT;
		}
	}

error:
	mutex_unlock(&portal_mutex);
	return retval;
}

static ssize_t portal_write(struct file *filp, const char *buffer, size_t length, loff_t *offset)
{
	char writeBuffer[MAX_PORTAL_MSG];
	int retval;

	mutex_lock(&portal_mutex);

	if (!portal_dev) {
		retval = -ENOENT;
		goto error;
	}

	length = min(length, sizeof(writeBuffer));
	retval = copy_from_user(writeBuffer, buffer, length);
	if (retval) {
		dev_dbg(&portal_dev->udev->dev, "copy_from_user could not copy %d bytes in portal_write\n", retval);
		retval = -EFAULT;
	} else {
		retval = usb_control_msg(portal_dev->udev, usb_sndctrlpipe(portal_dev->udev, 0), USB_REQ_SET_CONFIGURATION, USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x200, 0, (void*)writeBuffer, length, HZ / 30);
		if (retval < 0)
			dev_dbg(&portal_dev->udev->dev, "usb_control_msg for portal_write with length=%d returned %d\n", length, retval);
	}

error:
	mutex_unlock(&portal_mutex);
	return retval;
}

static const struct file_operations portal_fops = {
	.owner = THIS_MODULE,
	.read = portal_read,
	.write = portal_write,
};

static int portal_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	int retval = -ENOMEM;

	if (portal_dev) {
		dev_err(&interface->dev, "Only one portal is supported\n");
		retval = -EEXIST;
		goto error_skip;
	}

	portal_dev = kzalloc(sizeof(struct usb_portal), GFP_KERNEL);
	if (portal_dev == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error_skip;
	}

	portal_dev->udev = usb_get_dev(udev);

	usb_set_intfdata(interface, portal_dev);

	//portal_dev = portal_dev;
	retval = register_chrdev(PORTAL_MAJOR, DRIVER_NAME, &portal_fops);
	if (retval < 0)
		goto error;

	dev_info(&interface->dev, DEVICE_NAME " now attached\n");
	return 0;

error:
	mutex_lock(&portal_mutex);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(portal_dev->udev);
	kfree(portal_dev);
	portal_dev = 0;
	mutex_unlock(&portal_mutex);

error_skip:
	return retval;
}

static void portal_disconnect(struct usb_interface *interface)
{
	struct usb_portal *dev;

	mutex_lock(&portal_mutex);
	unregister_chrdev(PORTAL_MAJOR, DEVICE_NAME);
	dev = usb_get_intfdata(interface);

	if (portal_dev == dev)
		portal_dev = 0;

	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);

	mutex_unlock(&portal_mutex);

	dev_info(&interface->dev, DEVICE_NAME " now disconnected\n");
}

static struct usb_driver portal_driver = {
	.name =        DRIVER_NAME,
	.probe =       portal_probe,
	.disconnect =  portal_disconnect,
	.id_table =    id_table,
};

static int __init usb_portal_init(void)
{
	int retval = 0;
	retval = usb_register(&portal_driver);
	if (retval)
		printk("usb_register failed. Error number %d\n", retval);
	return retval;
}

static void __exit usb_portal_exit(void)
{
	usb_deregister(&portal_driver);
}

module_init(usb_portal_init);
module_exit(usb_portal_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

