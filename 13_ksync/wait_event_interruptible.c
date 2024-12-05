#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/delay.h>


static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int condition = 0;
static struct task_struct *my_thread;

static ktime_t sleep_time;
static ktime_t wake_time;

static int my_thread_fn(void *data) {
    printk(KERN_INFO "Thread started\n");

    while (!kthread_should_stop()) {
         
        wait_event_interruptible(my_wait_queue, condition != 0);
         wake_time = ktime_get(); // 记录被唤醒的时间
        if (condition == 1) {
            printk(KERN_INFO "Condition met, thread running\n");
            condition = 0;
        }
        // 计算唤醒所耗时间 [Thu Dec  5 16:07:37 2024] Wake-up time: 69191 ns
        s64 elapsed_time_ns = ktime_to_ns(ktime_sub(wake_time, sleep_time));
        printk(KERN_INFO "Wake-up time: %lld ns\n", elapsed_time_ns);
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

    msleep(5000); // 模拟一些延迟

    condition = 1;
    sleep_time = ktime_get(); // 记录进入睡眠的时间
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
MODULE_DESCRIPTION("A simple example of waking up a process in the Linux kernel");