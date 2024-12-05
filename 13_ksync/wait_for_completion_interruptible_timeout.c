#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static DECLARE_COMPLETION(my_completion);
static struct task_struct *my_thread;

static int my_thread_fn(void *data) {
    printk(KERN_INFO "Thread started\n");

    while (!kthread_should_stop()) {
        if (wait_for_completion_interruptible_timeout(&my_completion, msecs_to_jiffies(5000)) == 0) {
            printk(KERN_INFO "Timeout occurred\n");
        } else {
            printk(KERN_INFO "Completion signaled, thread running\n");
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

    complete(&my_completion);

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
MODULE_DESCRIPTION("A simple example of using completion in the Linux kernel");
