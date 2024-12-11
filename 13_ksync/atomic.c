/*原子操作（atomic operations）是一种用于实现线程间同步的机制。原子操作可以确保对共享变量的操作是不可分割的，从而避免竞争条件。*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/atomic.h>

static struct task_struct *thread1;
static struct task_struct *thread2;
static atomic_t shared_data = ATOMIC_INIT(0);

static int thread_fn1(void *data) {
    printk(KERN_INFO "Thread 1 started\n");

    while (!kthread_should_stop()) {
        atomic_inc(&shared_data);
        printk(KERN_INFO "Thread 1 incremented shared_data to %d\n", atomic_read(&shared_data));
        msleep(1000); // 模拟一些工作
    }

    printk(KERN_INFO "Thread 1 stopping\n");
    return 0;
}

static int thread_fn2(void *data) {
    printk(KERN_INFO "Thread 2 started\n");

    while (!kthread_should_stop()) {
        atomic_inc(&shared_data);
        printk(KERN_INFO "Thread 2 incremented shared_data to %d\n", atomic_read(&shared_data));
        msleep(1000); // 模拟一些工作
    }

    printk(KERN_INFO "Thread 2 stopping\n");
    return 0;
}

static int __init atomic_example_init(void) {
    printk(KERN_INFO "Initializing atomic example module\n");

    // 创建线程1
    thread1 = kthread_run(thread_fn1, NULL, "thread1");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "Failed to create thread 1\n");
        return PTR_ERR(thread1);
    }

    // 创建线程2
    thread2 = kthread_run(thread_fn2, NULL, "thread2");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "Failed to create thread 2\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    return 0;
}

static void __exit atomic_example_exit(void) {
    printk(KERN_INFO "Exiting atomic example module\n");

    // 停止线程1
    if (thread1) {
        kthread_stop(thread1);
    }

    // 停止线程2
    if (thread2) {
        kthread_stop(thread2);
    }
}

module_init(atomic_example_init);
module_exit(atomic_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using atomic operations to notify threads in the Linux kernel");