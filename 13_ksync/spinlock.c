/*自旋锁（spinlock）是一种用于保护共享资源的同步机制。自旋锁在等待锁释放时会不断地循环检查锁的状态，
因此适用于短时间持有锁的场景。自旋锁不会导致线程睡眠，因此在中断上下文中也可以使用。*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

static struct task_struct *thread1;
static struct task_struct *thread2;
static spinlock_t my_spinlock;
static int shared_data = 0;

static int thread_fn1(void *data) {
    printk(KERN_INFO "Thread 1 started\n");

    while (!kthread_should_stop()) {
        spin_lock(&my_spinlock);
        shared_data++;
        printk(KERN_INFO "Thread 1 incremented shared_data to %d\n", shared_data);
        spin_unlock(&my_spinlock);
        msleep(1000); // 模拟一些工作
    }

    printk(KERN_INFO "Thread 1 stopping\n");
    return 0;
}

static int thread_fn2(void *data) {
    printk(KERN_INFO "Thread 2 started\n");

    while (!kthread_should_stop()) {
        spin_lock(&my_spinlock);
        shared_data++;
        printk(KERN_INFO "Thread 2 incremented shared_data to %d\n", shared_data);
        spin_unlock(&my_spinlock);
        msleep(1000); // 模拟一些工作
    }

    printk(KERN_INFO "Thread 2 stopping\n");
    return 0;
}

static int __init spinlock_example_init(void) {
    printk(KERN_INFO "Initializing spinlock example module\n");

    // 初始化自旋锁
    spin_lock_init(&my_spinlock);

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

static void __exit spinlock_example_exit(void) {
    printk(KERN_INFO "Exiting spinlock example module\n");

    // 停止线程1
    if (thread1) {
        kthread_stop(thread1);
    }

    // 停止线程2
    if (thread2) {
        kthread_stop(thread2);
    }
}

module_init(spinlock_example_init);
module_exit(spinlock_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using spinlock to notify threads in the Linux kernel");