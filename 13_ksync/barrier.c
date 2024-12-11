#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/completion.h>

#define NUM_THREADS 3

static struct task_struct *threads[NUM_THREADS];
static DECLARE_COMPLETION(barrier);

static int thread_fn(void *data) {
    int id = (int)(long)data;
    printk(KERN_INFO "Thread %d started\n", id);

    // 模拟一些工作
    msleep(1000 * id);

    printk(KERN_INFO "Thread %d waiting at barrier\n", id);
    complete(&barrier); // 通知到达屏障

    // 等待所有线程到达屏障
    wait_for_completion(&barrier);

    printk(KERN_INFO "Thread %d passed the barrier\n", id);

    return 0;
}

static int __init barrier_example_init(void) {
    int i;
    printk(KERN_INFO "Initializing barrier example module\n");

    // 创建线程
    for (i = 0; i < NUM_THREADS; i++) {
        threads[i] = kthread_run(thread_fn, (void *)(long)i, "thread%d", i);
        if (IS_ERR(threads[i])) {
            printk(KERN_ERR "Failed to create thread %d\n", i);
            return PTR_ERR(threads[i]);
        }
    }

    // 等待所有线程到达屏障
    for (i = 0; i < NUM_THREADS; i++) {
        wait_for_completion(&barrier);
    }

    // 重新初始化屏障以便所有线程继续执行
    for (i = 0; i < NUM_THREADS; i++) {
        complete(&barrier);
    }

    return 0;
}

static void __exit barrier_example_exit(void) {
    int i;
    printk(KERN_INFO "Exiting barrier example module\n");

    // 停止所有线程
    for (i = 0; i < NUM_THREADS; i++) {
        if (threads[i]) {
            kthread_stop(threads[i]);
        }
    }
}

module_init(barrier_example_init);
module_exit(barrier_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using barrier to notify threads in the Linux kernel");