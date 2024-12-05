#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>

static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue);
static int condition = 0;
static struct task_struct *my_thread;
static struct hrtimer my_timer;

enum hrtimer_restart timer_callback(struct hrtimer *timer) {
    condition = 1;
    wake_up_interruptible(&my_wait_queue);
    return HRTIMER_NORESTART;
}

static int my_thread_fn(void *data) {
    printk(KERN_INFO "Thread started\n");

    while (!kthread_should_stop()) {
        wait_event_interruptible(my_wait_queue, condition != 0);
        if (condition == 1) {
            printk(KERN_INFO "Condition met, thread running\n");
            condition = 0;
        }
    }

    printk(KERN_INFO "Thread stopping\n");
    return 0;
}

static int __init my_module_init(void) {
    ktime_t ktime = ktime_set(5, 0); // 5 seconds

    printk(KERN_INFO "Module loaded\n");

    my_thread = kthread_run(my_thread_fn, NULL, "my_thread");
    if (IS_ERR(my_thread)) {
        printk(KERN_ERR "Failed to create thread\n");
        return PTR_ERR(my_thread);
    }

    hrtimer_init(&my_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    my_timer.function = timer_callback;
    hrtimer_start(&my_timer, ktime, HRTIMER_MODE_REL);

    return 0;
}

static void __exit my_module_exit(void) {
    if (my_thread) {
        kthread_stop(my_thread);
    }
    hrtimer_cancel(&my_timer);

    printk(KERN_INFO "Module unloaded\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using hrtimer and wait_queue in the Linux kernel");