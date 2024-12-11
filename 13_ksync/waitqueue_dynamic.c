lude <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/err.h>

uint32_t read_count = 0;
static struct task_struct *wait_thread;

dev_t dev = 0;
static struct class *dev_class;
static struct cdev my_cdev;
wait_queue_head_t my_waitqueue;
int waitqueue_flag = 0;

static int wait_function(void *unused)
{

    while (1)
    {
        pr_info("Waiting For Event...\n");
        wait_event_interruptible(my_waitqueue, waitqueue_flag != 0);
        if (waitqueue_flag == 2)
        {
            pr_info("Event Came From Exit Function\n");
            return 0;
        }
        pr_info("Event Came From Read Function - %d\n", ++read_count);
        waitqueue_flag = 0;
    }
    do_exit(0);
    return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    waitqueue_flag = 1;
    wake_up_interruptible(&my_waitqueue);
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = my_read,
};

static int __init my_driver_init(void)
{
    if ((alloc_chrdev_region(&dev, 0, 1, "my_dev")) < 0)
        return -1;

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    my_cdev.ops = &fops;

    if ((cdev_add(&my_cdev, dev, 1)) < 0)
        goto r_class;

    if (IS_ERR(dev_class = class_create(THIS_MODULE, "my_class")))
        goto r_class;

    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "my_device")))
        goto r_device;

    init_waitqueue_head(&my_waitqueue);

    if ((wait_thread = kthread_create(wait_function, NULL, "WaitThread")))
        wake_up_process(wait_thread);

    return 0;

r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev, 1);
    return -1;
}

static void __exit my_driver_exit(void)
{
    waitqueue_flag = 2;
    wake_up_interruptible(&my_waitqueue);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev, 1);
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cp <cp@gmail.com>");
MODULE_DESCRIPTION("Simple linux driver");
MODULE_VERSION("1.7");

