#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int condition = 0;
static struct task_struct *my_thread;
static int my_thread_fn(void *data) {
    long timeout = msecs_to_jiffies(5000); // 超时时间为 5000 毫秒

    printk(KERN_INFO "Thread started\n");

    while (!kthread_should_stop()) {
        long ret = wait_event_interruptible_timeout(my_wait_queue, condition != 0, timeout);
        if (ret == 0) {
            printk(KERN_INFO "Timeout occurred\n");
        } else if (ret > 0) {
            printk(KERN_INFO "Condition met, thread running\n");
            condition = 0;
        } else {
            printk(KERN_INFO "Interrupted by signal\n");
        }
    }

    printk(KERN_INFO "Thread stopping\n");
    return 0;
}

static int __init my_module_init(void) {
    printk(KERN_INFO "Module loaded\n");

    my_thread = kthread_run(my_thread_fn, NULL, "my_thread");
    if (IS_ERR(my_thread)) {
        printk(KERN_ERR "Failed to create thread\n");
        return PTR_ERR(my_thread);
    }

    msleep(2000); // 模拟一些延迟

    condition = 1;
    wake_up_interruptible(&my_wait_queue);

    return 0;
}

static void __exit my_module_exit(void) {
    if (my_thread) {
        kthread_stop(my_thread);
    }

    printk(KERN_INFO "Module unloaded\n");
}
module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using wait_event_interruptible_timeout in the Linux kernel");